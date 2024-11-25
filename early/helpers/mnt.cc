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
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>

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

struct mntopt {
    char const *name;
    unsigned long flagmask;
    unsigned long flagset;
    unsigned long flagno;
};

static constexpr unsigned long MS_TMASK = MS_BIND | MS_MOVE | MS_REMOUNT;

static mntopt known_opts[] = {
    {"async",       MS_SYNCHRONOUS, 0,              MS_SYNCHRONOUS},
    {"atime",       MS_NOATIME,     0,              MS_NOATIME},
    {"bind",        MS_TMASK,       MS_BIND,        0},
    {"dev",         MS_NODEV,       0,              MS_NODEV},
    {"diratime",    MS_NODIRATIME,  0,              MS_NODIRATIME},
    {"dirsync",     MS_DIRSYNC,     MS_DIRSYNC,     0},
    {"exec",        MS_NOEXEC,      0,              MS_NOEXEC},
    {"lazytime",    MS_LAZYTIME,    MS_LAZYTIME,    0},
    {"move",        MS_TMASK,       MS_MOVE,        0},
    {"recurse",     MS_REC,         MS_REC,         0},
    {"relatime",    MS_RELATIME,    MS_RELATIME,    0},
    {"remount",     MS_TMASK,       MS_REMOUNT,     0},
    {"ro",          MS_RDONLY,      MS_RDONLY,      0},
    {"rw",          MS_RDONLY,      0,              MS_RDONLY},
    {"silent",      MS_SILENT,      MS_SILENT,      0},
    {"strictatime", MS_STRICTATIME, MS_STRICTATIME, 0},
    {"suid",        MS_NOSUID,      0,              MS_NOSUID},
    {"symfollow",   MS_NOSYMFOLLOW, 0,              MS_NOSYMFOLLOW},
    {"sync",        MS_SYNCHRONOUS, MS_SYNCHRONOUS, 0},
    {"verbose",     MS_SILENT,      0,              MS_SILENT},
};

static unsigned long parse_mntopts(
    char *opts, unsigned long flags, std::string &eopts
) {
    if (!opts) {
        return flags;
    }
    for (char *s; (s = strsep(&opts, ","));) {
        char *optn = s;
        bool isno = ((optn[0] == 'n') && (optn[1] == 'o'));
        if (isno) {
            optn += 2;
        }
        if (!optn[0]) {
            continue;
        }
        mntopt *optv = nullptr;
        for (size_t i = 0; i < (sizeof(known_opts) / sizeof(mntopt)); ++i) {
            auto cmpv = std::strcmp(optn, known_opts[i].name);
            if (cmpv == 0) {
                optv = &known_opts[i];
                flags &= ~optv->flagmask;
                flags |= (isno ? optv->flagno : optv->flagset);
                break;
            } else if (cmpv < 0) {
                /* no point in searching further */
                break;
            }
        }
        /* not recognized... */
        if (!optv) {
            if (!eopts.empty()) {
                eopts.push_back(',');
            }
            eopts += s;
        }
    }
    return flags;
}

static int do_mount(
    char const *tgt, char const *src, char const *fstype, char *opts
) {
    std::string eopts{};
    unsigned long flags = parse_mntopts(opts, MS_SILENT, eopts);
    if (mount(src, tgt, fstype, flags, eopts.data()) < 0) {
        warn("mount");
        return 1;
    }
    return 0;
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

int main(int argc, char **argv) {
    if (argc < 2) {
        errx(1, "not enough arguments");
    }

    if (!std::strcmp(argv[1], "is")) {
        if (argc != 3) {
            errx(1, "incorrect number of arguments");
        }
        return do_is(argv[2]);
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
    }

    warnx("unknown command '%s'", argv[1]);
    return 1;
}
