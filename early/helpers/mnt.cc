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
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <linux/loop.h>

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
    char *opts, unsigned long flags, std::string &eopts,
    std::string *loopdev = nullptr, std::string *offset = nullptr,
    std::string *sizelimit = nullptr
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
        /* not recognized or manually handled */
        if (!optv) {
            /* skip stuff that is not to be passed */
            if (((optn[0] == 'X') || (optn[0] == 'x')) && (optn[1] == '-')) {
                continue;
            }
            if (!std::strcmp(optn, "defaults")) {
                /* this resets some of the flags */
                flags &= ~(MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_SYNCHRONOUS);
                continue;
            }
            if (loopdev) {
                if (!std::strncmp(optn, "loop", 4) && ((optn[4] == '=') || !optn[4])) {
                    *loopdev = optn;
                    continue;
                }
                auto *eq = std::strchr(optn, '=');
                if (eq) {
                    /* maybe params */
                    if (!std::strncmp(optn, "offset", eq - optn)) {
                        *offset = eq + 1;
                        continue;
                    } else if (!std::strncmp(optn, "sizelimit", eq - optn)) {
                        *sizelimit = eq + 1;
                        continue;
                    }
                }
            }
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

static bool loop_match(
    int fd, struct stat const &fst, uint64_t offset, uint64_t sizelimit,
    unsigned long &flags
) {
    loop_info64 linf;
    if (fd <= 0) {
        return false;
    }
    if (ioctl(fd, LOOP_GET_STATUS64, &linf)) {
        return false;
    }
    if (
        (linf.lo_device == fst.st_dev) &&
        (linf.lo_inode == fst.st_ino) &&
        (linf.lo_offset == offset) &&
        (linf.lo_sizelimit == sizelimit)
    ) {
        if (linf.lo_flags & LO_FLAGS_READ_ONLY) {
            flags |= MS_RDONLY;
        }
        return true;
    }
    return false;
}

static int open_loop(
    int mode, struct stat const &fst, uint64_t offset,
    uint64_t sizelimit, std::string &src, bool &configure,
    unsigned long &flags
) {
    char dbuf[64];

    /* first open /dev as a base point for everything */
    auto dfd = open("/dev", O_DIRECTORY | O_RDONLY);
    if (dfd < 0) {
        warn("could not open /dev");
        return -1;
    }
    /* internal version for fdopendir */
    auto dfdd = dup(dfd);
    if (dfdd < 0) {
        warn("could not dup /dev fd");
        close(dfd);
        return -1;
    }
    /* now open it for looping... */
    auto *dr = fdopendir(dfdd);
    if (!dr) {
        warn("could not fdopendir /dev");
        close(dfd);
        return -1;
    }
    /* then try finding a loop device that is preconfigured with
     * the params we need, and if we find one, just use it
     */
    for (;;) {
        errno = 0;
        auto *dp = readdir(dr);
        if (!dp) {
            if (errno == 0) {
                closedir(dr);
                break;
            }
            warn("could not read from /dev");
            close(dfd);
            closedir(dr);
            return -1;
        }
        if (std::strncmp(dp->d_name, "loop", 4)) {
            /* irrelevant */
            continue;
        }
        if (!std::strcmp(dp->d_name, "loop-control")) {
            /* also not */
            continue;
        }
        /* potential loopdev */
        auto lfd = openat(dfd, dp->d_name, mode);
        if (loop_match(lfd, fst, offset, sizelimit, flags)) {
            std::snprintf(dbuf, sizeof(dbuf), "/dev/%s", dp->d_name);
            src = dbuf;
            configure = false;
            closedir(dr);
            close(dfd);
            return lfd;
        }
        close(lfd);
    }
    /* did not find a preconfigured one, so grab a free one */
    auto cfd = openat(dfd, "loop-control", O_RDWR);
    if (cfd < 0) {
        warn("could not open /dev/loop-control");
        close(dfd);
        return -1;
    }
    auto rv = ioctl(cfd, LOOP_CTL_GET_FREE, 0);
    if (rv < 0) {
        warn("could not find a free loop device");
        close(cfd);
        close(dfd);
        return -1;
    }
    close(cfd);
    std::snprintf(dbuf, sizeof(dbuf), "/dev/loop%d", rv);
    /* try opening with the wanted mode */
    src = dbuf;
    auto ret = openat(dfd, &dbuf[5], mode);
    close(dfd);
    return ret;
}

static int setup_loop(
    std::string const &loopdev, std::string const &offsetp,
    std::string const &sizelimitp, std::string &src, int &afd,
    unsigned long &flags
) {
    char const *lsrc = loopdev.data();
    auto *eq = std::strchr(lsrc, '=');
    /* loop file descriptor and source file descriptor */
    int lfd = -1, ffd = -1;
    /* parse the options */
    uint64_t sizelimit = 0, offset = 0;
    if (!offsetp.empty()) {
        char *errp = nullptr;
        offset = std::strtoull(offsetp.data(), &errp, 10);
        if (!errp || *errp) {
            warnx("failed to parse loop offset");
            return -1;
        }
    }
    if (!sizelimitp.empty()) {
        char *errp = nullptr;
        sizelimit = std::strtoull(sizelimitp.data(), &errp, 10);
        if (!errp || *errp) {
            warnx("failed to parse loop sizelimit");
            return -1;
        }
    }
    /* open the source file first... */
    int lmode = (flags & MS_RDONLY) ? O_RDONLY : O_RDWR;
    ffd = open(src.data(), lmode);
    /* try readonly as a fallback */
    if (ffd < 0 && (lmode != O_RDONLY) && (errno == EROFS)) {
        lmode = O_RDONLY;
        flags |= MS_RDONLY;
        ffd = open(src.data(), lmode);
    }
    if (ffd < 0) {
        warn("failed to open source file");
        return -1;
    }
    /* stat it for later checking */
    struct stat fst;
    if (fstat(ffd, &fst)) {
        warn("failed to stat source file");
        close(ffd);
        return -1;
    }
    /* pre-create a loop configuration */
    struct loop_config loopc;
    std::memset(&loopc, 0, sizeof(loopc));
    loopc.fd = ffd;
    loopc.info.lo_offset = offset;
    loopc.info.lo_sizelimit = sizelimit;
    loopc.info.lo_flags = LO_FLAGS_AUTOCLEAR | (
        (lmode == O_RDONLY) ? LO_FLAGS_READ_ONLY : 0
    );
    if (src.size() >= LO_NAME_SIZE) {
        std::memcpy(loopc.info.lo_file_name, src.data(), LO_NAME_SIZE - 1);
    } else {
        std::memcpy(loopc.info.lo_file_name, src.data(), src.size());
    }
    /* now see if we have to configure at all */
    bool configure = true;
    if (!eq || !eq[1]) {
        /* find unused loop device, or a preconfigured one */
        lfd = open_loop(lmode, fst, offset, sizelimit, src, configure, flags);
    } else {
        lfd = open(eq + 1, lmode);
        if (loop_match(lfd, fst, offset, sizelimit, flags)) {
            configure = false;
        }
        src = eq + 1;
    }
    if (lfd < 0) {
        warn("failed to open loop device");
        close(ffd);
        return -1;
    }
    /* if the loop is preconfigured, we're good; src was already set */
    if (!configure) {
        return 0;
    }
    /* finally configure */
    if (ioctl(lfd, LOOP_CONFIGURE, &loopc)) {
        warn("failed to configure the loop device");
        close(ffd);
        close(lfd);
        return -1;
    }
    close(ffd);
    afd = lfd;
    return 0;
}

static int do_mount(
    char const *tgt, char const *src, char const *fstype, char *opts
) {
    std::string eopts{};
    /* potential loop device */
    std::string asrc = src;
    std::string loopdev{};
    /* parameters for loop */
    std::string offset{};
    std::string sizelimit{};
    /* do the initial parse pass */
    unsigned long flags = parse_mntopts(
        opts, MS_SILENT, eopts, &loopdev, &offset, &sizelimit
    );
    /* if loop was requested, set it up */
    int afd = -1;
    auto oflags = flags;
    if (!loopdev.empty()) {
        auto ret = setup_loop(loopdev, offset, sizelimit, asrc, afd, flags);
        if (ret < 0) {
            return ret;
        }
        if (!(oflags & MS_RDONLY) && (flags & MS_RDONLY)) {
            warnx("Source file write-protected, mounting read-only.");
        }
    } else {
        asrc = src;
    }
    auto ret = do_mount_raw(tgt, asrc.data(), fstype, flags, eopts);
    /* close after mount is done so it does not autodestroy */
    close(afd);
    return ret;
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
    /* ensure a new enough kernel is used to avoid bugs and missing
     * syscalls and whatever other issues that are likely to happen
     */
    utsname ubuf;
    if (uname(&ubuf)) {
        warn("could not get uname");
        return 1;
    }
    char *ustr = ubuf.release;
    char *uerr = nullptr;
    auto umaj = std::strtoul(ustr, &uerr, 10);
    if ((umaj < 5) || !uerr || (*uerr != '.')) {
        warnx("kernels older than 5.x are not supported");
        return 1;
    }
    if (umaj == 5) {
        ustr = uerr + 1;
        uerr = nullptr;
        auto umin = std::strtoul(ustr, &uerr, 10);
        if (umin < 10) {
            warnx("kernels older than 5.10 are not supported");
            return 1;
        }
    }
    /* try remounting / with the params we want; this may fail depending on fs */
    do_remount("/", root_opts);
    /* other initial pseudofs... */
    if (do_try("/sys", "sysfs", "sysfs", procsys_opts)) {
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
