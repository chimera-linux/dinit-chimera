/*
 * Clock setup helper program
 *
 * Meant to be used during system init and shutdown; on start, it will
 * set the kernel timezone (without messing with system clock, as during
 * bootup it is already set from hardware clock), while on stop, it will
 * set hardware clock from system clock.
 *
 * Created as a thin replacement for the complicated hwclock program from
 * util-linux, intended to do only the bootup/shutdown tasks and nothing
 * else.
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
#include <cerrno>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
/* RTC_SET_TIME */
#include <linux/rtc.h>

#include "clock_common.hh"

typedef enum {
    OPT_START,
    OPT_STOP,
} opt_t;

static int usage(char **argv) {
    printf("usage: %s start|stop [utc|localtime]\n", argv[0]);
    return 1;
}

static int do_settimeofday(struct timezone const *tz) {
#if !defined(SYS_settimeofday) && defined(SYS_settimeofday_time32)
    int ret = syscall(SYS_settimeofday_time32, 0, tz);
#else
    int ret = syscall(SYS_settimeofday, 0, tz);
#endif
    if (ret) {
        warn("settimeofday");
    }
    return (ret != 0);
}

static int do_start(rtc_mod_t mod) {
    struct timezone tz = {};
    int ret = 0;
    struct tm *lt;
    time_t ct;

    /* for UTC, lock warp_clock and PCIL */
    if (mod == RTC_MOD_UTC) {
        ret = do_settimeofday(&tz);
        if (ret) {
            goto done;
        }
    }

    ct = time(nullptr);
    lt = localtime(&ct);
    tz.tz_minuteswest = (-lt->tm_gmtoff / 60);

    /* set kernel timezone; lock warp_clock and set PCIL if non-UTC */
    if ((mod != RTC_MOD_UTC) || (tz.tz_minuteswest != 0)) {
        ret = do_settimeofday(&tz);
    }

done:
    return ret;
}

static int do_stop(rtc_mod_t mod) {
    struct timeval tv;
    struct tm tmt = {};
    /* open rtc; it may be busy, so loop */
    int fd = -1;

    char const *rtcs[] = {"/dev/rtc", "/dev/rtc0", nullptr};
    char const **crtc = rtcs;

    while (*crtc++) {
        fd = open(*crtc, O_WRONLY);
        int attempts = 8; /* do not stall longer than 15 * 8 sec == 2 minutes */
        while ((fd < 0) && (errno == EBUSY) && attempts--) {
            usleep(15000);
            fd = open(*crtc, O_WRONLY);
        }
        if (fd < 0) {
            /* exists but still busy, fail */
            if (errno == EBUSY) {
                return 1;
            }
            /* another error, see if we can move on */
            continue;
        }
        /* got fd */
        break;
    }

    /* didn't manage to open any fd */
    if (fd < 0) {
        return 1;
    }

    /* should not fail though */
    if (gettimeofday(&tv, nullptr) < 0) {
        close(fd);
        return 1;
    }

    /* set up tmt */
    if (mod == RTC_MOD_UTC) {
        gmtime_r(&tv.tv_sec, &tmt);
    } else {
        localtime_r(&tv.tv_sec, &tmt);
    }
    tmt.tm_isdst = 0;

    int ret = syscall(SYS_ioctl, fd, RTC_SET_TIME, &tmt);
    close(fd);

    return (ret != 0);
}

int main(int argc, char **argv) {
    /* insufficient arguments */
    if ((argc <= 1) || (argc > 3)) {
        return usage(argv);
    }

    opt_t opt;
    rtc_mod_t mod;

    if (!strcmp(argv[1], "start")) {
        opt = OPT_START;
    } else if (!strcmp(argv[1], "stop")) {
        opt = OPT_STOP;
    } else {
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

    if (opt == OPT_START) {
        return do_start(mod);
    }

    return do_stop(mod);
}
