/*
 * Device monitor daemon
 *
 * The device monitor daemon opens a control socket and lets clients
 * watch for device availability. It keeps the connection for as long
 * as the device remains available.
 *
 * The protocol is a simple stream protocol; a client makes a connection
 * and sends a handshake byte (0xDD) followed by a 6 byte type string and
 * a null terminator, two bytes of value length, and N bytes of value (no null)
 *
 * At this point, the server will respond at least once, provided the handshake
 * is not malformed (in which case the connection will terminate); the response
 * bytes are either 0 (device not available) or 1 (device available); it will
 * send more bytes (assuming neither side terminates the connection) as the
 * state changes
 *
 * Once a connection is established the server will never terminate it unless
 * an error happens in the server; only the client can do so
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
#define _GNU_SOURCE /* accept4 */
#endif

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

/* selfpipe for signals */
static int sigpipe[2] = {-1, -1};
pollfd sigfd{};

static void sig_handler(int sign) {
    write(sigpipe[1], &sign, sizeof(sign));
}

int main(int argc, char **argv) {
    if (argc > 2) {
        errx(1, "usage: %s [fd]", argv[0]);
    }

    int fdnum = -1;
    if (argc > 1) {
        fdnum = atoi(argv[1]);
        errno = 0;
        if (!fdnum || (fcntl(fdnum, F_GETFD) < 0)) {
            errx(1, "invalid file descriptor for readiness (%d)", fdnum);
        }
    }

    /* simple signal handler for SIGTERM/SIGINT */
    {
        struct sigaction sa{};
        sa.sa_handler = sig_handler;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }

    std::printf("devmon: start\n");

    /* signal pipe */
    if (pipe(sigpipe) < 0) {
        warn("pipe failed");
        return 1;
    }
    sigfd.fd = sigpipe[0];
    sigfd.events = POLLIN;
    sigfd.revents = 0;

    /* readiness as soon as we're bound to a socket */
    if (fdnum > 0) {
        std::printf("devmon: readiness notification\n");
        write(fdnum, "READY=1\n", sizeof("READY=1"));
        close(fdnum);
    }

    std::printf("devmon: main loop\n");

    int ret = 0;
    for (;;) {
        std::printf("devmon: poll\n");
        auto pret = poll(&sigfd, 1, -1);
        if (pret < 0) {
            if (errno == EINTR) {
                continue;
            }
            warn("poll failed");
            ret = 1;
            break;
        } else if (pret == 0) {
            continue;
        }
        /* signal fd */
        if (sigfd.revents == POLLIN) {
            int sign;
            if (read(sigfd.fd, &sign, sizeof(sign)) != sizeof(sign)) {
                warn("signal read failed");
                continue;
            }
            /* sigterm or sigint */
            break;
        }
        if (ret) {
            break;
        }
    }
    close(sigfd.fd);

    std::printf("devmon: exit with %d\n", ret);
    return ret;
}
