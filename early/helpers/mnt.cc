/*
 * A helper for mounts
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 q66 <q66@chimera-linux.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mntent.h>
#include <err.h>
#include <grp.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* fallback; not accurate but good enough for early boot */
static int mntpt_noproc(char const *inpath, struct stat *st) {
    dev_t sdev;
    ino_t sino;
    char *path;
    size_t slen;

    sdev = st->st_dev;
    sino = st->st_ino;

    /* can't detect file bindmounts without proc */
    if (!S_ISDIR(st->st_mode)) {
        return 1;
    }

    slen = strlen(inpath);
    path = static_cast<char *>(malloc(slen + 4));
    if (!path) {
        return 1;
    }

    snprintf(path, slen + 4, "%s/..", inpath);
    if (stat(path, st)) {
        return 1;
    }

    /* different device -> mount point
     * same inode -> most likely root
     */
    free(path);
    return (st->st_dev == sdev) && (st->st_ino != sino);
}

static int do_is(char const *mntpt) {
    struct stat st;
    FILE *sf;
    struct mntent *mn;
    char *path;
    int retval = 1;

    /* symbolic link or not given */
    if (lstat(mntpt, &st) || S_ISLNK(st.st_mode)) {
        return 1;
    }

    sf = setmntent("/proc/self/mounts", "r");
    if (!sf) {
        return mntpt_noproc(mntpt, &st);
    }

    path = realpath(mntpt, nullptr);
    if (!path) {
        return 1;
    }

    while ((mn = getmntent(sf))) {
        if (!strcmp(mn->mnt_dir, path)) {
            retval = 0;
            break;
        }
    }

    endmntent(sf);
    free(path);
    return retval;
}

static constexpr unsigned long MS_TMASK = MS_BIND | MS_MOVE | MS_REMOUNT;
static constexpr unsigned long MS_AMASK = MS_NOATIME | MS_RELATIME;

struct mntopt {
    char const *name;
    unsigned long flagmask;
    unsigned long flagset;
    bool invert;
};

static mntopt known_opts[] = {
    {"async", MS_SYNCHRONOUS, MS_SYNCHRONOUS, true},
    {"atime", MS_AMASK, MS_NOATIME, true},
    {"bind", MS_TMASK, MS_BIND, false},
    {"dev", MS_NODEV, MS_NODEV, true},
    {"diratime", MS_NODIRATIME, MS_NODIRATIME, true},
    {"dirsync", MS_DIRSYNC, MS_DIRSYNC, false},
    {"exec", MS_NOEXEC, MS_NOEXEC, true},
    {"iversion", MS_I_VERSION, MS_I_VERSION, false},
    {"lazytime", MS_LAZYTIME, MS_LAZYTIME, false},
    {"loud", MS_SILENT, MS_SILENT, true},
    {"mand", MS_MANDLOCK, MS_MANDLOCK, false},
    {"move", MS_TMASK, MS_MOVE, false},
    {"noatime", MS_AMASK, MS_NOATIME, false},
    {"nodev", MS_NODEV, MS_NODEV, false},
    {"nodiratime", MS_NODIRATIME, MS_NODIRATIME, false},
    {"noexec", MS_NOEXEC, MS_NOEXEC, false},
    {"noiversion", MS_I_VERSION, MS_I_VERSION, true},
    {"nolazytime", MS_LAZYTIME, MS_LAZYTIME, true},
    {"nomand", MS_MANDLOCK, MS_MANDLOCK, true},
    {"norelatime", MS_AMASK, MS_RELATIME, true},
    {"nostrictatime", MS_STRICTATIME, MS_STRICTATIME, true},
    {"nosuid", MS_NOSUID, MS_NOSUID, false},
    {"nosymfollow", MS_NOSYMFOLLOW, MS_NOSYMFOLLOW, false},
    {"private", MS_PRIVATE, MS_PRIVATE, false},
    {"rbind", MS_TMASK, MS_BIND | MS_REC, false},
    {"relatime", MS_AMASK, MS_RELATIME, false},
    {"remount", MS_TMASK, MS_REMOUNT, false},
    {"ro", MS_RDONLY, MS_RDONLY, false},
    {"rprivate", MS_PRIVATE, MS_PRIVATE | MS_REC, false},
    {"rshared", MS_SHARED, MS_SHARED | MS_REC, false},
    {"rslave", MS_SLAVE, MS_SLAVE | MS_REC, false},
    {"runbindable", MS_UNBINDABLE, MS_UNBINDABLE | MS_REC, false},
    {"rw", MS_RDONLY, MS_RDONLY, true},
    {"silent", MS_SILENT, MS_SILENT, false},
    {"shared", MS_SHARED, MS_SHARED, false},
    {"slave", MS_SLAVE, MS_SLAVE, false},
    {"strictatime", MS_STRICTATIME, MS_STRICTATIME, false},
    {"suid", MS_NOSUID, MS_NOSUID, true},
    {"symfollow", MS_NOSYMFOLLOW, MS_NOSYMFOLLOW, true},
    {"sync", MS_SYNCHRONOUS, MS_SYNCHRONOUS, false},
    {"unbindable", MS_UNBINDABLE, MS_UNBINDABLE, false},
};

static unsigned long parse_mntopts(
    char *opts, unsigned long flags, std::string &eopts
) {
    if (!opts) {
        return flags;
    }
    for (char *optn; (optn = strsep(&opts, ","));) {
        if (!optn[0]) {
            continue;
        }
        mntopt *optv = nullptr;
        for (size_t i = 0; i < (sizeof(known_opts) / sizeof(mntopt)); ++i) {
            auto cmpv = std::strcmp(optn, known_opts[i].name);
            if (cmpv == 0) {
                optv = &known_opts[i];
                flags &= ~optv->flagmask;
                if (optv->invert) {
                    flags &= ~optv->flagset;
                } else {
                    flags |= optv->flagset;
                }
                break;
            } else if (cmpv < 0) {
                /* no point in searching further */
                break;
            }
        }
        if (!optv && !std::strcmp(optn, "defaults")) {
            /* this resets some of the flags */
            flags &= ~(MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_SYNCHRONOUS);
            continue;
        }
        /* not recognized... */
        if (!optv) {
            if (!eopts.empty()) {
                eopts.push_back(',');
            }
            eopts += optn;
        }
    }
    return flags;
}

static std::string unparse_mntopts(unsigned long flags, std::string const &eopts) {
    std::string ret{};
    for (size_t i = 0; i < (sizeof(known_opts) / sizeof(mntopt)); ++i) {
        auto &ko = known_opts[i];
        if (ko.invert || !(flags & ko.flagset)) {
            continue;
        }
        switch (ko.flagset) {
            case MS_PRIVATE:
            case MS_SHARED:
            case MS_SLAVE:
            case MS_UNBINDABLE:
                /* these should not be passed through */
                continue;
            case MS_REC:
                if (!(flags & MS_BIND)) {
                    continue;
                }
                break;
        }
        if (!ret.empty()) {
            ret.push_back(',');
        }
        ret += ko.name;
    }
    /* TODO: filter these too... */
    if (!eopts.empty()) {
        if (!ret.empty()) {
            ret.push_back(',');
        }
        ret += eopts;
    }
    return ret;
}

static int parse_umntopts(char *opts) {
    if (!opts) {
        return 0;
    }
    int flags = 0;
    for (char *s; (s = strsep(&opts, ","));) {
        if (!std::strcmp(s, "force")) {
            flags |= MNT_FORCE;
        } else if (!std::strcmp(s, "detach")) {
            flags |= MNT_DETACH;
        }
    }
    return flags;
}

static int do_mount_helper(
    char const *tgt, char const *src, char const *fstype,
    unsigned long flags, std::string const &eopts
) {
    char hname[256];
    snprintf(hname, sizeof(hname), "/sbin/mount.%s", fstype);
    if (access(hname, X_OK) < 0) {
        return -1;
    }
    auto opts = unparse_mntopts(flags, eopts);
    auto cpid = fork();
    if (cpid < 0) {
        warn("fork failed");
        return 1;
    }
    if (cpid == 0) {
        /* child, exec the helper */
        execl(hname, hname, "-o", opts.c_str(), src, tgt, 0);
        abort();
    }
    int status;
    while (waitpid(cpid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        warn("waitpid failed");
        return 1;
    }
    return 0;
}

static int do_mount_raw(
    char const *tgt, char const *src, char const *fstype,
    unsigned long flags, std::string &eopts, bool helper = false
) {
    unsigned long pflags = flags;
    unsigned long pmask = MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE;
    /* propagation flags need to be set separately! */
    if (pflags & pmask) {
        pflags &= pmask | MS_REC;
        flags &= ~(pmask | MS_REC);
    }
    if (helper) {
        /* if false, helper may still be tried but *after* internal mount */
        auto hret = do_mount_helper(tgt, src, fstype, flags, eopts);
        if (hret >= 0) {
            return hret;
        }
    }
    if (mount(src, tgt, fstype, flags, eopts.data()) < 0) {
        int serrno = errno;
        /* try a helper if regular mount fails */
        int ret = do_mount_helper(tgt, src, fstype, flags, eopts);
        if (ret < 0) {
            errno = serrno;
            warn("failed to mount filesystem '%s'", tgt);
            return 1;
        }
        return ret;
    }
    /* propagation flags should change separately */
    if ((pflags & pmask) && (mount(src, tgt, fstype, pflags, nullptr) < 0)) {
        warn("failed to change propagation flags of '%s'", tgt);
        return 1;
    }
    return 0;
}

static int do_mount(
    char const *tgt, char const *src, char const *fstype, char *opts
) {
    std::string eopts{};
    unsigned long flags = parse_mntopts(opts, MS_SILENT, eopts);
    return do_mount_raw(tgt, src, fstype, flags, eopts);
}

static int do_try(
    char const *tgt, char const *src, char const *fstype, char *opts
) {
    /* already mounted */
    if (do_is(tgt) == 0) {
        return 0;
    }
    return do_mount(tgt, src, fstype, opts);
}

static int do_try_maybe(
    char const *tgt, char const *src, char const *fstype, char *opts
) {
    struct stat st;
    /* don't bother if we can't mount it there */
    if (stat(tgt, &st) || !S_ISDIR(st.st_mode)) {
        return 0;
    }
    return do_try(tgt, src, fstype, opts);
}

static int do_remount(char const *tgt, char *opts) {
    unsigned long rmflags = MS_SILENT | MS_REMOUNT;
    std::string mtab_eopts{};
    struct mntent *mn = nullptr;
    /* preserve existing params */
    FILE *sf = setmntent("/proc/self/mounts", "r");
    if (!sf) {
        warn("could not open mtab");
        return 1;
    }
    while ((mn = getmntent(sf))) {
        if (!strcmp(mn->mnt_dir, tgt)) {
            /* found root */
            rmflags = parse_mntopts(mn->mnt_opts, rmflags, mtab_eopts);
            break;
        } else {
            mn = nullptr;
        }
    }
    endmntent(sf);
    if (!mn) {
        warnx("could not locate '%s' mount", tgt);
        return 1;
    }
    rmflags = parse_mntopts(opts, rmflags, mtab_eopts);
    /* and remount... */
    if (do_mount_raw(mn->mnt_dir, mn->mnt_fsname, mn->mnt_type, rmflags, mtab_eopts)) {
        return 1;
    }
    return 0;
}

static int do_umount(char const *tgt, char *opts) {
    if (umount2(tgt, parse_umntopts(opts)) < 0) {
        warn("umount2");
        return 1;
    }
    return 0;
}

static int do_prepare(char *root_opts) {
    char procsys_opts[] = "nosuid,noexec,nodev";
    char dev_opts[] = "mode=0755,nosuid";
    char shm_opts[] = "mode=1777,nosuid,nodev";
    /* first set umask to an unrestricted value */
    umask(0);
    /* first try mounting procfs and fail if we can't */
    if (do_try("/proc", "proc", "proc", procsys_opts)) {
        return 1;
    }
    /* try remounting / with the params we want */
    if (do_remount("/", root_opts)) {
        return 1;
    }
    /* other initial pseudofs... */
    if (do_try("/sys", "sys", "sysfs", procsys_opts)) {
        return 1;
    }
    if (do_try("/dev", "dev", "devtmpfs", dev_opts)) {
        return 1;
    }
    /* mountpoints for pts, shm; if these fail the mount will too */
    mkdir("/dev/pts", 0755);
    mkdir("/dev/shm", 0755);
    /* try getting the tty group */
    auto *ttyg = getgrnam("tty");
    char pts_opts[128];
    snprintf(
        pts_opts, sizeof(pts_opts), "mode=0620,gid=%u,nosuid,noexec",
        ttyg ? unsigned(ttyg->gr_gid) : 5
    );
    if (do_try("/dev/pts", "devpts", "devpts", pts_opts)) {
        return 1;
    }
    if (do_try("/dev/shm", "shm", "tmpfs", shm_opts)) {
        return 1;
    }
    /* stdio symlinks if necessary */
    if ((symlink("/proc/self/fd", "/dev/fd") < 0) && (errno != EEXIST)) {
        warn("could not create /dev/fd");
        return 1;
    }
    if ((symlink("/proc/self/fd/0", "/dev/stdin") < 0) && (errno != EEXIST)) {
        warn("could not create /dev/stdin");
        return 1;
    }
    if ((symlink("/proc/self/fd/1", "/dev/stdout") < 0) && (errno != EEXIST)) {
        warn("could not create /dev/stdout");
        return 1;
    }
    if ((symlink("/proc/self/fd/2", "/dev/stderr") < 0) && (errno != EEXIST)) {
        warn("could not create /dev/stderr");
        return 1;
    }
    /* auxiliary pseudofs */
    if (do_try_maybe("/sys/kernel/security", "securityfs", "securityfs", nullptr)) {
        warn("could not mount /sys/kernel/security");
        return 1;
    }
    if (do_try_maybe("/sys/firmware/efi/efivars", "efivarfs", "efivarfs", procsys_opts)) {
        warn("could not mount /sys/kernel/security");
        return 1;
    }
    if (do_try_maybe("/sys/fs/selinux", "selinuxfs", "selinuxfs", nullptr)) {
        warn("could not mount /sys/kernel/security");
        return 1;
    }
    /* success! */
    return 0;
}

static int do_root_rw() {
    /* remount / with requested parameters; if present in fstab, use those,
     * if not present, leave as-is except clear the rdonly flag
     */
    unsigned long rmflags = MS_SILENT | MS_REMOUNT;
    std::string fstab_eopts{};
    struct mntent *mn = nullptr;
    /* look up requested root mount in fstab first */
    FILE *sf = setmntent("/etc/fstab", "r");
    if (sf) {
        while ((mn = getmntent(sf))) {
            if (!strcmp(mn->mnt_dir, "/")) {
                /* found root */
                rmflags = parse_mntopts(mn->mnt_opts, rmflags, fstab_eopts);
                break;
            } else {
                mn = nullptr;
            }
        }
        endmntent(sf);
    } else if (errno != ENOENT) {
        warn("could not open fstab");
        return 1;
    }
    /* if not found, look it up in mtab instead, and strip ro flag */
    if (!mn) {
        sf = setmntent("/proc/self/mounts", "r");
        if (!sf) {
            warn("could not open mtab");
            return 1;
        }
        while ((mn = getmntent(sf))) {
            if (!strcmp(mn->mnt_dir, "/")) {
                /* found root */
                rmflags = parse_mntopts(mn->mnt_opts, rmflags, fstab_eopts);
                break;
            } else {
                mn = nullptr;
            }
        }
        rmflags &= ~MS_RDONLY;
        endmntent(sf);
    }
    if (!mn) {
        warnx("could not locate root mount");
        return 1;
    }
    /* and remount... */
    if (do_mount_raw(mn->mnt_dir, mn->mnt_fsname, mn->mnt_type, rmflags, fstab_eopts)) {
        return 1;
    }
    return 0;
}

static int do_getent(char const *tab, const char *mntpt, char const *ent) {
    FILE *sf = setmntent(tab, "r");
    if (!sf) {
        warn("could not open '%s'", tab);
        return 1;
    }
    for (struct mntent *mn; (mn = getmntent(sf));) {
        if (strcmp(mn->mnt_dir, mntpt)) {
            continue;
        }
        if (!std::strcmp(ent, "fsname")) {
            printf("%s\n", mn->mnt_fsname);
        } else if (!std::strcmp(ent, "type")) {
            printf("%s\n", mn->mnt_type);
        } else if (!std::strcmp(ent, "opts")) {
            printf("%s\n", mn->mnt_opts);
        } else if (!std::strcmp(ent, "freq")) {
            printf("%d\n", mn->mnt_freq);
        } else if (!std::strcmp(ent, "passno")) {
            printf("%d\n", mn->mnt_passno);
        } else {
            warnx("invalid field '%s'", ent);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        errx(1, "not enough arguments");
    }

    if (!std::strcmp(argv[1], "is")) {
        if (argc != 3) {
            errx(1, "incorrect number of arguments");
        }
        return do_is(argv[2]);
    } else if (!std::strcmp(argv[1], "prepare")) {
        if (argc != 3) {
            errx(1, "incorrect number of arguments");
        }
        return do_prepare(argv[2]);
    } else if (!std::strcmp(argv[1], "root-rw")) {
        if (argc != 2) {
            errx(1, "incorrect number of arguments");
        }
        return do_root_rw();
    } else if (!std::strcmp(argv[1], "try")) {
        if ((argc < 5) || (argc > 6)) {
            errx(1, "incorrect number of arguments");
        }
        return do_try(argv[2], argv[3], argv[4], (argc < 6) ? nullptr : argv[5]);
    } else if (!std::strcmp(argv[1], "mnt")) {
        if ((argc < 5) || (argc > 6)) {
            errx(1, "incorrect number of arguments");
        }
        return do_mount(argv[2], argv[3], argv[4], (argc < 6) ? nullptr : argv[5]);
    } else if (!std::strcmp(argv[1], "umnt")) {
        if ((argc < 3) || (argc > 4)) {
            errx(1, "incorrect number of arguments");
        }
        return do_umount(argv[2], (argc < 4) ? nullptr : argv[3]);
    } else if (!std::strcmp(argv[1], "rmnt")) {
        if (argc != 4) {
            errx(1, "incorrect number of arguments");
        }
        return do_remount(argv[2], argv[3]);
    } else if (!std::strcmp(argv[1], "getent")) {
        if (argc != 5) {
            errx(1, "incorrect number of arguments");
        }
        return do_getent(argv[2], argv[3], argv[4]);
    }

    warnx("unknown command '%s'", argv[1]);
    return 1;
}
