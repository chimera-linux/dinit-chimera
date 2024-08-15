/*
 * Loopback device bringup helper
 *
 * Does the same thing as `ip link set up dev lo`.
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

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <err.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

int main(void) {
    int fams[] = {PF_INET, PF_PACKET, PF_INET6, PF_UNSPEC};
    int fd = -1, serr = 0;

    for (int *fam = fams; *fam != PF_UNSPEC; ++fam) {
        fd = socket(*fam, SOCK_DGRAM, 0);
        if (fd >= 0) {
            break;
        } else if (!serr) {
            serr = errno; /* save first error */
        }
    }

    if (fd < 0) {
        errno = serr;
        err(1, "socket");
    }

    struct ifreq ifr;
    memcpy(ifr.ifr_name, "lo", 3);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        err(1, "SIOCGIFFLAGS");
    }

    if (ifr.ifr_flags & IFF_UP) {
        return 0;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        err(1, "SIOCSIFFLAGS");
    }

    return 0;
}
