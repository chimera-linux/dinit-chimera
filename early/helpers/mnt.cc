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

int main(int argc, char **argv) {
    if (argc < 2) {
        errx(1, "not enough arguments");
    }

    if (!std::strcmp(argv[1], "is")) {
        if (argc != 3) {
            errx(1, "incorrect number of arguments");
        }
        return do_is(argv[2]);
    }

    warnx("unknown command '%s'", argv[1]);
    return 1;
}
