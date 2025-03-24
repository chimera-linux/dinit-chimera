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

    bool isdev = !std::strncmp(devn, "/dev/", 5);
    bool issys = !std::strncmp(devn, "/sys/", 5);
    bool isnet = !std::strncmp(devn, "netif:", 3);
    bool ismac = !std::strncmp(devn, "mac:", 4);
    bool isusb = !std::strncmp(devn, "usb:", 4);

    if (!isdev && !isnet && !ismac && !issys && !isusb) {
        errx(1, "invalid device value");
    }

    /* default for device nodes */
    char const *type = "dev";
    if (issys) {
        type = "sys";
    } else if (!isdev) {
        /* terminate the devtype */
        auto *col = std::strchr(devn, ':');
        *col = '\0';
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

    if (connect(sock, reinterpret_cast<sockaddr const *>(&saddr), sizeof(saddr)) < 0) {
        err(1, "connect failed");
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
