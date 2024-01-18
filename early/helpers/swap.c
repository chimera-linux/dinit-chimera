/*
 * Swap helper
 *
 * Activates or deactivates all swap devices in fstab and /proc/swaps.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2023 q66 <q66@chimera-linux.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>
#include <mntent.h>
#include <sys/swap.h>
#include <sys/stat.h>

#ifndef SWAP_FLAG_DISCARD_ONCE
#define SWAP_FLAG_DISCARD_ONCE 0x20000
#endif
#ifndef SWAP_FLAG_DISCARD_PAGES
#define SWAP_FLAG_DISCARD_PAGES 0x40000
#endif

static int usage(char **argv) {
    fprintf(stderr, "usage: %s start|stop\n", argv[0]);
    return 1;
}

/* we must be able to resolve e.g. LABEL=swapname */
static char const *resolve_dev(char const *raw, char *buf, size_t bufsz) {
#define CHECK_PFX(name, lname) \
    if (!strncmp(raw, name "=", sizeof(name))) { \
        snprintf(buf, bufsz, "/dev/disk/by-" lname "/%s", raw + sizeof(name)); \
        return buf; \
    }

    CHECK_PFX("LABEL", "label")
    CHECK_PFX("UUID", "uuid")
    CHECK_PFX("PARTLABEL", "partlabel")
    CHECK_PFX("PARTUUID", "partuuid")
    CHECK_PFX("ID", "id")

    /* otherwise stat the input */
    return raw;
}

static int do_start(void) {
    struct mntent *m;
    int ret = 0;
    char devbuf[4096];
    char const *devname;
    FILE *f = setmntent("/etc/fstab", "r");
    if (!f) {
        if (errno == ENOENT) {
            return 0;
        }
        err(1, "fopen");
    }
    while ((m = getmntent(f))) {
        char *opt;
        struct stat st;
        int flags = 0;
        if (strcmp(m->mnt_type, "swap")) {
            continue;
        }
        if (hasmntopt(m, "noauto")) {
            continue;
        }
        opt = hasmntopt(m, "discard");
        if (opt) {
            opt += 7;
            flags |= SWAP_FLAG_DISCARD;
            if (*opt++ == '=') {
                if (!strncmp(opt, "once", 4) && (!opt[4] || (opt[4] == ','))) {
                    flags |= SWAP_FLAG_DISCARD_ONCE;
                } else if (
                    !strncmp(opt, "pages", 5) && (!opt[5] || (opt[5] == ','))
                ) {
                    flags |= SWAP_FLAG_DISCARD_PAGES;
                }
            }
        }
        opt = hasmntopt(m, "pri");
        if (opt) {
            opt += 3;
            if (*opt++ == '=') {
                char *err = NULL;
                unsigned long pval = strtoul(opt, &err, 10);
                if (pval > SWAP_FLAG_PRIO_MASK) {
                    pval = SWAP_FLAG_PRIO_MASK;
                }
                if (err && (!*err || (*err == ','))) {
                    flags |= SWAP_FLAG_PREFER | pval;
                }
            }
        }
        devname = resolve_dev(m->mnt_fsname, devbuf, sizeof(devbuf));
        if (stat(devname, &st)) {
            warn("stat failed for '%s'", m->mnt_fsname);
            ret = 1;
            continue;
        }
        if (S_ISREG(st.st_mode) && ((st.st_blocks * (off_t)512) < st.st_size)) {
            warnx("swap '%s' has holes", m->mnt_fsname);
            ret = 1;
            continue;
        }
        if (swapon(devname, flags)) {
            warn("swapon failed for '%s'", m->mnt_fsname);
            ret = 1;
            continue;
        }
    }
    endmntent(f);
    return ret;
}

static int do_stop(void) {
    int ret = 0;
    char devbuf[4096];
    char const *devname;
    /* first do /proc/swaps */
    FILE *f = fopen("/proc/swaps", "r");
    if (f) {
        char *line = NULL;
        size_t len = 0;
        ssize_t nread;
        while ((nread = getline(&line, &len, f)) != -1) {
            if (*line != '/') {
                continue;
            }
            char *p = strchr(line, ' ');
            if (p) {
                *p = '\0';
            }
            if (swapoff(line)) {
                warn("swapoff failed for swap '%s'", line);
                ret = 1;
            }
        }
        free(line);
        fclose(f);
    }
    /* then do fstab */
    f = setmntent("/etc/fstab", "r");
    if (f) {
        struct mntent *m;
        while ((m = getmntent(f))) {
            if (strcmp(m->mnt_type, "swap")) {
                continue;
            }
            devname = resolve_dev(m->mnt_fsname, devbuf, sizeof(devbuf));
            if (swapoff(devname) && (errno != EINVAL)) {
                warn("swapoff failed for '%s'", m->mnt_fsname);
                ret = 1;
            }
        }
        endmntent(f);
    }
    return ret;
}

int main(int argc, char **argv) {
    /* insufficient arguments */
    if ((argc != 2) || getuid()) {
        return usage(argv);
    }

    if (!strcmp(argv[1], "start")) {
        return do_start();
    } else if (!strcmp(argv[1], "stop")) {
        return do_stop();
    }

    return usage(argv);
}
