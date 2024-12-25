/*
 * Date/time adjustment helper
 *
 * A helper program that will adjust system date/time closer to reality
 * in absence of a reasonably functional RTC. It works by taking a known
 * file in the system, checking its timestamp, and adjusting system date
 * if it's less.
 *
 * On shutdown, it will update the modification time of said file to a
 * new value.
 *
 * Additionally, on systems with an RTC that is not writable, it will
 * account for the time offset in order to keep the system date/time
 * current.
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <ctime>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <err.h>

#include "clock_common.hh"

#ifndef LOCALSTATEDIR
#define LOCALSTATEDIR "/var/lib"
#endif

#define TS_DIR LOCALSTATEDIR "/swclock"
#define TS_FILE "timestamp"
#define TS_OFFSET "offset"
#define RTC_NODE "/sys/class/rtc/rtc0/since_epoch"

static int usage(char **argv) {
    fprintf(stderr, "usage: %s start|stop\n", argv[0]);
    return 1;
}

static int stat_reg(int dfd, char const *fpath, struct stat *st) {
    if (fstatat(dfd, fpath, st, AT_SYMLINK_NOFOLLOW) < 0) {
        return -1;
    }
    if (!S_ISREG(st->st_mode)) {
        return -1;
    }
    return 0;
}

bool convert_localtime(rtc_mod_t mod, unsigned long long &rtc_epoch) {
    time_t rtc_lt;
    struct tm *rtc_lm;
    /* if not localtime, don't do anything */
    if (mod != RTC_MOD_LOCALTIME) {
        return true;
    }
    /* give up if we have 32-bit time_t and the rtc value does not fit */
    if ((sizeof(time_t) == 4) && (rtc_epoch > INT32_MAX)) {
        return false;
    }
    rtc_lt = (time_t)rtc_epoch;
    /* gmtime assumes UTC, lie; the result is a localtime struct tm */
    rtc_lm = gmtime(&rtc_lt);
    if (!rtc_lm) {
        return false;
    }
    /* convert our localtime to UTC */
    rtc_lt = mktime(rtc_lm);
    if (rtc_lt < 0) {
        return false;
    }
    rtc_epoch = (unsigned long long)rtc_lt;
    return true;
}

static int do_start(int dfd, time_t curt, rtc_mod_t mod) {
    struct timeval tv = {};
    struct stat st;
    FILE *rtcf, *offf;
    char rtc_epochs[32];
    char offsets[32];
    char *errp = nullptr;
    unsigned long long rtc_epoch, offset;
    int offfd;

    /* check if an offset file exists */
    offfd = openat(dfd, TS_OFFSET, O_RDONLY);
    if (offfd < 0) {
        goto regular_set;
    }

    /* check if the rtc node exists */
    rtcf = fopen(RTC_NODE, "r");
    if (!rtcf) {
        goto regular_set;
    }

    offf = fdopen(offfd, "r");
    if (!offf) {
        close(offfd);
        err(1, "fdopen");
    }

    /* read the rtc */
    if (!fgets(rtc_epochs, sizeof(rtc_epochs), rtcf)) {
        fclose(rtcf);
        fclose(offf);
        goto regular_set;
    }
    fclose(rtcf);

    /* read the offset */
    if (!fgets(offsets, sizeof(offsets), offf)) {
        fclose(offf);
        goto regular_set;
    }
    fclose(offf);

    /* convert */
    rtc_epoch = strtoull(rtc_epochs, &errp, 10);
    if (!rtc_epoch || !errp || (*errp && (*errp != '\n'))) {
        /* junk value */
        goto regular_set;
    }

    /* rtc may be stored in utc or localtime
     * if it's localtime, adjust by timezone
     */
    if (!convert_localtime(mod, rtc_epoch)) {
        goto regular_set;
    }

    errp = nullptr;
    offset = strtoull(offsets, &errp, 10);
    if (!offset || !errp || (*errp && (*errp != '\n'))) {
        /* junk value */
        goto regular_set;
    }

    rtc_epoch += offset;
    /* give up if we have 32-bit time_t and the rtc value does not fit */
    if ((sizeof(time_t) == 4) && (rtc_epoch > INT32_MAX)) {
        goto regular_set;
    }
    /* see if the new time is newer */
    if ((time_t)rtc_epoch < curt) {
        /* nope */
        goto regular_set;
    }

    /* set it in place of the timestamp */
    tv.tv_sec = (time_t)rtc_epoch;
    goto do_set;

regular_set:
    /* no or bogus timestamp */
    if (stat_reg(dfd, TS_FILE, &st) < 0) {
        return 0;
    }

    tv.tv_sec = st.st_atime;
    /* timestamp is older than we have right now */
    if (tv.tv_sec < curt) {
        return 0;
    }

do_set:
    /* set it */
    if (settimeofday(&tv, nullptr) < 0) {
        err(1, "settimeofday");
    }

    return 0;
}

static int do_stop(int dfd, time_t curt, rtc_mod_t mod) {
    struct timespec times[2] = {};
    char epochs[32];
    char *errp = nullptr;
    unsigned long long epoch;
    FILE *rtcf;
    int ofd, fd;

    /* unlink the old offset file just in case */
    unlinkat(dfd, TS_OFFSET, 0);

    /* check if rtc node exists */
    rtcf = fopen(RTC_NODE, "r");
    if (!rtcf) {
        goto regular_save;
    }

    /* read it */
    if (!fgets(epochs, sizeof(epochs), rtcf)) {
        fclose(rtcf);
        goto regular_save;
    }
    fclose(rtcf);

    /* convert */
    epoch = strtoull(epochs, &errp, 10);
    if (!epoch || !errp || (*errp && (*errp != '\n'))) {
        /* junk value */
        goto regular_save;
    }

    /* if the rtc is in localtime, adjust to current time */
    if (!convert_localtime(mod, epoch)) {
        /* could not adjust, don't save offset */
        goto regular_save;
    }

    /* diff it against current time */
    if ((unsigned long long)curt <= epoch) {
        /* do not save zero or negative offset; it means the rtc is updating */
        goto regular_save;
    }

    /* save offset before saving the regular timestamp */
    ofd = openat(
        dfd, TS_OFFSET, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_NOFOLLOW, 0600
    );
    if (ofd < 0) {
        err(1, "offset open failed");
    }

    rtcf = fdopen(ofd, "w");
    if (!rtcf) {
        close(ofd);
        err(1, "fdopen");
    }

    /* write the offset */
    fprintf(rtcf, "%llu", (unsigned long long)curt - epoch);
    fclose(rtcf);
    /* but touch the regular timestamp too */

regular_save:
    /* create the timestamp if needed */
    fd = openat(
        dfd, TS_FILE,
        O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_NOATIME, 0600
    );
    if (fd < 0) {
        err(1, "timestamp open failed");
    }

    times[0].tv_sec = times[1].tv_sec = curt;
    if (futimens(fd, times) < 0) {
        err(1, "futimens");
    }
    close(fd);

    return 0;
}

int main(int argc, char **argv) {
    struct timeval ctv;
    rtc_mod_t mod;

    /* insufficient arguments */
    if ((argc <= 1) || (argc > 3) || getuid()) {
        return usage(argv);
    }

    if (argc > 2) {
        if (!strcmp(argv[2], "utc")) {
            mod = RTC_MOD_UTC;
        } else if (!strcmp(argv[2], "localtime")) {
            mod = RTC_MOD_LOCALTIME;
        } else {
            return usage(argv);
        }
    } else {
        mod = rtc_mod_guess();
    }

    if (gettimeofday(&ctv, nullptr) < 0) {
        err(1, "gettimeofday");
    }

    umask(0077);

    if ((mkdir(TS_DIR, 0700) < 0) && (errno != EEXIST)) {
        err(1, "unable to create swclock stamp directory");
    }

    int dfd = open(TS_DIR, O_DIRECTORY | O_RDONLY);
    if ((dfd < 0) || (flock(dfd, LOCK_EX) < 0)) {
        err(1, "unable to lock swclock stamp directory");
    }

    if (!strcmp(argv[1], "start")) {
        return do_start(dfd, ctv.tv_sec, mod);
    } else if (!strcmp(argv[1], "stop")) {
        return do_stop(dfd, ctv.tv_sec, mod);
    }

    return usage(argv);
}
