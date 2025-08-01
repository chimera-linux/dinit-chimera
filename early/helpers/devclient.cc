/*
 * Device monitor client program
 *
 * The client program is meant to be spawned per device watch and
 * stays running as long as the device remains available; it will
 * not signal readiness until the device has become available.
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

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef DEVMON_SOCKET
#error monitor socket is not provided
#endif

int main(int argc, char **argv) {
    if (argc != 3) {
        errx(1, "usage: %s devname fd", argv[0]);
    }

    int fdnum = atoi(argv[2]);
    errno = 0;
    if (!fdnum || (fcntl(fdnum, F_GETFD) < 0)) {
        errx(1, "invalid file descriptor for readiness (%d)", fdnum);
    }

    char *devn = argv[1];
    char const *type;
    std::string rdev;

    auto *eq = std::strchr(devn, '=');
    if (eq) {
        /* e.g. device@PARTLABEL=foo */
        *eq = '\0';
#define RESOLVE_PFX(name, lname) \
        if (!std::strcmp(devn, name)) { \
            rdev = "/dev/disk/by-" lname "/"; \
            rdev += eq + 1; \
            devn = rdev.data(); \
        }
        RESOLVE_PFX("LABEL", "label")
        else RESOLVE_PFX("UUID", "uuid")
        else RESOLVE_PFX("PARTLABEL", "partlabel")
        else RESOLVE_PFX("PARTUUID", "partuuid")
        else RESOLVE_PFX("ID", "id")
        else {
            errx(1, "invalid device prefix '%s'", devn);
        }
        type = "dev";
    } else if (!std::strncmp(devn, "/dev/", 5)) {
        /* device@/dev/foo */
        type = "dev";
    } else if (!std::strncmp(devn, "/sys/", 5)) {
        /* device@/sys/foo */
        type = "sys";
    } else {
        /* e.g. device@netif:eth0, etc. */
        auto *col = std::strchr(devn, ':');
        if (!col) {
            errx(1, "invalid device value");
        }
        *col = '\0';
        if (
            std::strcmp(devn, "netif") &&
            std::strcmp(devn, "mac") &&
            std::strcmp(devn, "usb")
        ) {
            errx(1, "invalid device value");
        }
        type = devn;
        devn = col + 1;
    }

    unsigned short devlen = std::strlen(devn);
    if (!devlen) {
        errx(1, "devname must not be empty");
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        err(1, "socket failed");
    }

    sockaddr_un saddr;
    std::memset(&saddr, 0, sizeof(saddr));

    saddr.sun_family = AF_UNIX;
    std::memcpy(saddr.sun_path, DEVMON_SOCKET, sizeof(DEVMON_SOCKET));

    /* handshake sequence */
    unsigned char wz[8 + sizeof(unsigned short)];
    std::memset(wz, 0, sizeof(wz));
    wz[0] = 0xDD;
    std::memcpy(&wz[1], type, std::strlen(type));
    std::memcpy(&wz[8], &devlen, sizeof(devlen));

    for (;;) {
        if (!connect(sock, reinterpret_cast<sockaddr const *>(&saddr), sizeof(saddr))) {
            break;
        }
        switch (errno) {
            case EINTR:
                continue;
            case ENOENT:
                /* socket is not yet present... */
                break;
            case ENOTDIR:
                /* paths are not yet set up correctly */
                break;
            case ECONNREFUSED:
                /* socket is not yet listening, is a leftover, etc. */
                break;
            default:
                /* any other case, fail */
                err(1, "connect failed");
                break;
        }
        /* wait 250ms until next attempt */
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 250 * 1000000;
        nanosleep(&ts, nullptr);
    }
    std::printf("connected to devmon...\n");

    if (write(sock, wz, sizeof(wz)) != sizeof(wz)) {
        err(1, "protocol write failed");
    }
    if (write(sock, devn, devlen) != devlen) {
        err(1, "data write failed");
    }
    std::printf("wrote handshake data...\n");

    /* now read some bytes */
    for (;;) {
        unsigned char c;
        if (read(sock, &c, sizeof(c)) != sizeof(c)) {
            if (errno == EINTR) {
                continue;
            }
            err(1, "read failed");
        }
        if (c && (fdnum >= 0)) {
            /* it's there, signal readiness */
            std::printf("signal readiness...\n");
            write(fdnum, "READY=1\n", sizeof("READY=1"));
            close(fdnum);
            fdnum = -1;
        } else if (!c && (fdnum < 0)) {
            /* it was ready before and is not now, so exit */
            std::printf("device disappeared, quit...\n");
            close(sock);
            break;
        }
    }

    return 0;
}
