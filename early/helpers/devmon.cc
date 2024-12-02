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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef HAVE_UDEV
#include <libudev.h>

/* subsystems we always match even without a tag */
static char const *notag_subsys[] = {
    "block",
    "net",
    "tty",
    nullptr
};
#endif

#ifndef DEVMON_SOCKET
#error monitor socket is not provided
#endif

enum {
    DEVICE_DEV = 1,
    DEVICE_NETIF,
    DEVICE_MAC,
};

static bool sock_new(char const *path, int &sock, mode_t mode) {
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock < 0) {
        warn("socket failed");
        return false;
    }

    /* set buffers */
    int bufsz = 2048;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz)) < 0) {
        warn("setsockopt failed");
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz)) < 0) {
        warn("setsockopt failed");
    }

    std::printf("socket: created %d for %s\n", sock, path);

    sockaddr_un un;
    std::memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;

    auto plen = std::strlen(path);
    if (plen >= sizeof(un.sun_path)) {
        warnx("socket path '%s' too long", path);
        close(sock);
        return false;
    }

    std::memcpy(un.sun_path, path, plen + 1);
    /* no need to check this */
    unlink(path);

    if (bind(sock, reinterpret_cast<sockaddr const *>(&un), sizeof(un)) < 0) {
        warn("bind failed");
        close(sock);
        return false;
    }

    std::printf("socket: bound %d for %s\n", sock, path);

    if (chmod(path, mode) < 0) {
        warn("chmod failed");
        goto fail;
    }

    if (listen(sock, SOMAXCONN) < 0) {
        warn("listen failed");
        goto fail;
    }

    std::printf("socket: done\n");
    return true;

fail:
    unlink(path);
    close(sock);
    return false;
}

struct conn {
    char handshake[8] = {};
    int fd = -1;
    int devtype = 0;
    unsigned short datalen = 0;
    std::string datastr;
};

/* selfpipe for signals */
static int sigpipe[2] = {-1, -1};
/* event loop fds */
static std::vector<pollfd> fds{};
/* connections being established */
static std::vector<conn> conns{};
/* control socket */
static int ctl_sock = -1;

/* type mappings */
static std::unordered_map<std::string_view, std::string_view> map_dev{};
static std::unordered_map<std::string_view, std::string_view> map_netif{};
static std::unordered_map<std::string_view, std::string_view> map_mac{};

static bool check_devnode(std::string const &node, char const *devn = nullptr) {
    if (!devn && (map_dev.find(node) != map_dev.end())) {
        return true;
    } else if (devn && (node == devn)) {
        return true;
    }
    /* otherwise check if we're dealing with a link */
    struct stat st;
    if (lstat(node.c_str(), &st) || !S_ISLNK(st.st_mode)) {
        return false;
    }
    /* resolve... */
    auto *respath = realpath(node.c_str(), nullptr);
    if (!respath) {
        if (errno == ENOMEM) {
            abort();
        }
        return false;
    }
    /* check resolved in the set */
    bool ret;
    if (!devn) {
        ret = (map_dev.find(respath) != map_dev.end());
    } else {
        ret = !std::strcmp(respath, devn);
    }
    std::free(respath);
    return ret;
}

static void write_conn(conn &cn, unsigned char igot) {
    if (write(cn.fd, &igot, sizeof(igot)) != sizeof(igot)) {
        warn("write failed for %d\n", cn.fd);
        for (auto &fd: fds) {
            if (fd.fd == cn.fd) {
                fd.fd = -1;
                fd.revents = 0;
                break;
            }
        }
        close(cn.fd);
        cn.fd = -1;
    }
}

static void write_net(int devt, unsigned char igot, std::string const &name) {
    for (auto &cn: conns) {
        if ((cn.devtype != devt) || (cn.datastr != name)) {
            continue;
        }
        write_conn(cn, igot);
    }
}

static void write_dev(unsigned char igot, std::string const &name) {
    for (auto &cn: conns) {
        if (cn.devtype != DEVICE_DEV) {
            continue;
        }
        if (!check_devnode(cn.datastr, name.c_str())) {
            continue;
        }
        if (write(cn.fd, &igot, sizeof(igot)) != sizeof(igot)) {
            warn("write failed for %d\n", cn.fd);
            for (auto &fd: fds) {
                if (fd.fd == cn.fd) {
                    fd.fd = -1;
                    fd.revents = 0;
                    break;
                }
            }
            close(cn.fd);
            cn.fd = -1;
        }
    }
}

struct device {
    std::string name{}; /* devpath or ifname */
    std::string mac{};
    std::string syspath{};
    std::string subsys{};
    std::vector<std::string> waits_for{};

    void init_dev(char const *node, bool write = true) {
        if (node) {
            name = node;
        }
        std::printf(
            "devmon: add %s '%s'\n", subsys.c_str(), name.c_str()
        );
        if (node) {
            if (write) {
                write_dev(1, name);
            }
            map_dev.emplace(name, syspath);
        }
    }

    void init_net(char const *ifname, char const *macaddr, bool write = true) {
        if (ifname) {
            name = ifname;
        }
        if (macaddr) {
            mac = macaddr;
        }
        std::printf(
            "devmon: add netif '%s' ('%s')\n", name.c_str(), mac.c_str()
        );
        if (ifname) {
            if (write) {
                write_net(DEVICE_NETIF, 1, name);
            }
            map_netif.emplace(name, syspath);
        }
        if (macaddr) {
            if (write) {
                write_net(DEVICE_MAC, 1, mac);
            }
            map_mac.emplace(mac, syspath);
        }
    }

    void set_dev(char const *devnode) {
        if ((devnode && (name == devnode)) || (!devnode && name.empty())) {
            return;
        }
        std::printf(
            "devmon: device change '%s' -> '%s'\n",
            name.c_str(), devnode ? devnode : ""
        );
        write_dev(0, name);
        map_dev.erase(name);
        if (devnode) {
            name = devnode;
            write_dev(1, name);
            map_dev.emplace(name, syspath);
        } else {
            name.clear();
        }
    }

    void set_ifname(char const *ifname) {
        if ((ifname && (name == ifname)) || (!ifname && name.empty())) {
            return;
        }
        std::printf(
            "devmon: ifname change '%s' -> '%s'\n",
            name.c_str(), ifname ? ifname : ""
        );
        write_net(DEVICE_NETIF, 0, name);
        map_netif.erase(name);
        if (ifname) {
            name = ifname;
            write_net(DEVICE_NETIF, 1, name);
            map_netif.emplace(name, syspath);
        } else {
            name.clear();
        }
    }

    void set_mac(char const *nmac) {
        if ((nmac && (mac == nmac)) || (!nmac && mac.empty())) {
            return;
        }
        std::printf(
            "devmon: mac change '%s' -> '%s'\n",
            mac.c_str(), nmac ? nmac : ""
        );
        write_net(DEVICE_MAC, 0, mac);
        map_mac.erase(mac);
        if (nmac) {
            mac = nmac;
            write_net(DEVICE_MAC, 1, mac);
            map_mac.emplace(name, syspath);
        } else {
            mac.clear();
        }
    }

    void remove() {
        if (subsys == "net") {
            std::printf(
                "devmon: drop netif '%s' (mac: '%s')\n",
                name.c_str(), mac.c_str()
            );
            if (!name.empty()) {
                write_net(DEVICE_NETIF, 0, name);
                map_netif.erase(name);
            }
            if (!mac.empty()) {
                write_net(DEVICE_MAC, 0, mac);
                map_mac.erase(name);
            }
        } else {
            std::printf(
                "devmon: drop %s '%s'\n", subsys.c_str(), name.c_str()
            );
            if (!name.empty()) {
                write_dev(0, name);
                map_dev.erase(name);
            }
        }
    }
};

/* canonical mapping of syspath to devices, also holds the memory */
static std::unordered_map<std::string, device> map_sys;

/* service set */
static std::unordered_set<std::string> svc_set{};

#ifdef HAVE_UDEV
static struct udev *udev;
#endif

static void sig_handler(int sign) {
    write(sigpipe[1], &sign, sizeof(sign));
}

#ifdef HAVE_UDEV
static void handle_device_dinit(struct udev_device *dev, char const *, bool rem) {
    /* only tagged devices may have DINIT_WAITS_FOR when added
     * for removals, do a lookup to drop a possible service
     */
    if (
        !rem &&
        !udev_device_has_tag(dev, "dinit") &&
        !udev_device_has_tag(dev, "systemd")
    ) {
        return;
    }
#if 0
    auto *svc = udev_device_get_property_value(dev, "DINIT_WAITS_FOR");
#endif
}

static bool initial_populate(struct udev_enumerate *en) {
    if (udev_enumerate_scan_devices(en) < 0) {
        std::fprintf(stderr, "could not scan enumerate\n");
        return false;
    }

    struct udev_list_entry *en_devices = udev_enumerate_get_list_entry(en);
    struct udev_list_entry *en_entry;

    udev_list_entry_foreach(en_entry, en_devices) {
        auto *path = udev_list_entry_get_name(en_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);
        if (!dev) {
            std::fprintf(stderr, "could not construct device from enumerate\n");
            udev_enumerate_unref(en);
            return false;
        }
        auto &devm = map_sys[path];
        devm.syspath = path;
        handle_device_dinit(dev, path, false);
        devm.subsys = udev_device_get_subsystem(dev);
        if (devm.subsys != "net") {
            devm.init_dev(udev_device_get_devnode(dev), false);
        } else {
            devm.init_net(
                udev_device_get_sysname(dev),
                udev_device_get_sysattr_value(dev, "address"),
                false
            );
        }
    }
    return true;
}

static void add_device(
    struct udev_device *dev, char const *sysp, char const *ssys
) {
    /* construct a new device structure with new values */
    device devm;
    devm.syspath = sysp;
    devm.subsys = ssys;
    auto odev = map_sys.find(sysp);
    if (!std::strcmp(ssys, "net")) {
        auto *ifname = udev_device_get_sysname(dev);
        auto *macaddr = udev_device_get_sysattr_value(dev, "address");
        if (odev != map_sys.end()) {
            odev->second.set_ifname(ifname);
            odev->second.set_mac(macaddr);
            return;
        }
        devm.init_net(ifname, macaddr);
    } else {
        auto *node = udev_device_get_devnode(dev);
        if (odev != map_sys.end()) {
            odev->second.set_dev(node);
            return;
        }
        devm.init_dev(node);
    }
    map_sys.emplace(devm.syspath, std::move(devm));
}

static void remove_device(char const *sysp) {
    auto it = map_sys.find(sysp);
    if (it == map_sys.end()) {
        return;
    }
    auto &devm = it->second;
    devm.remove();
    auto sysn = std::move(devm.syspath);
    map_sys.erase(it);
}

static bool resolve_device(struct udev_monitor *mon, bool tagged) {
    auto *dev = udev_monitor_receive_device(mon);
    if (!dev) {
        warn("udev_monitor_receive_device failed");
        return false;
    }
    auto *sysp = udev_device_get_syspath(dev);
    auto *ssys = udev_device_get_subsystem(dev);
    if (!sysp || !ssys) {
        warn("could not get syspath or subsystem for device");
        return false;
    }
    /* when checking tagged monitor ensure we don't handle devices we
     * take care of unconditionally regardless of tag (another monitor)
     */
    for (auto **p = notag_subsys; *p; ++p) {
        if (!tagged) {
            break;
        }
        if (!std::strcmp(ssys, *p)) {
            udev_device_unref(dev);
            return true;
        }
    }
    /* whether to drop it */
    bool rem = !std::strcmp(udev_device_get_action(dev), "remove");
    std::printf("devmon: %s device '%s'\n", rem ? "drop" : "add", sysp);
    /* try handling dinit services... */
    handle_device_dinit(dev, sysp, rem);
    if (rem) {
        remove_device(sysp);
    } else {
        add_device(dev, sysp, ssys);
    }
    udev_device_unref(dev);
    return true;
}
#endif

int main(void) {
    /* simple signal handler for SIGTERM/SIGINT */
    {
        struct sigaction sa{};
        sa.sa_handler = sig_handler;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }

    umask(077);

    std::printf("devmon: start\n");

    /* signal pipe */
    {
        if (pipe(sigpipe) < 0) {
            warn("pipe failed");
            return 1;
        }
        auto &pfd = fds.emplace_back();
        pfd.fd = sigpipe[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
    }

    std::printf("devmon: socket init\n");

    /* control socket */
    {
        if (!sock_new(DEVMON_SOCKET, ctl_sock, 0700)) {
            return 1;
        }
        auto &pfd = fds.emplace_back();
        pfd.fd = ctl_sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
    }

    fds.reserve(16);
    conns.reserve(16);

#ifdef HAVE_UDEV
    std::printf("devmon: udev init\n");
    udev = udev_new();
    if (!udev) {
        std::fprintf(stderr, "could not create udev\n");
        return 1;
    }

    /* prepopulate the mappings */
    struct udev_enumerate *en1 = udev_enumerate_new(udev);
    struct udev_enumerate *en2 = udev_enumerate_new(udev);

    if (!en1 || !en2) {
        std::fprintf(stderr, "could not create udev enumerate\n");
        udev_unref(udev);
        return 1;
    }

    if (
        (udev_enumerate_add_match_tag(en2, "systemd") < 0) ||
        (udev_enumerate_add_match_tag(en2, "dinit") < 0)
    ) {
        std::fprintf(stderr, "could not add udev enumerate matches\n");
        udev_enumerate_unref(en1);
        udev_enumerate_unref(en2);
        udev_unref(udev);
        return 1;
    }

    for (auto **p = notag_subsys; *p; ++p) {
        if (
            (udev_enumerate_add_match_subsystem(en1, *p) < 0) ||
            (udev_enumerate_add_nomatch_subsystem(en2, *p) < 0)
        ) {
            std::fprintf(stderr, "could not add enumerate match for '%s'\n", *p);
            udev_enumerate_unref(en1);
            udev_enumerate_unref(en2);
            udev_unref(udev);
            return 1;
        }
    }

    if (!initial_populate(en1) || !initial_populate(en2)) {
        udev_enumerate_unref(en1);
        udev_enumerate_unref(en2);
        udev_unref(udev);
        return 1;
    }

    udev_enumerate_unref(en1);
    udev_enumerate_unref(en2);

    struct udev_monitor *mon1 = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon1) {
        std::fprintf(stderr, "could not create udev monitor\n");
        udev_unref(udev);
        return 1;
    }

    struct udev_monitor *mon2 = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon1) {
        std::fprintf(stderr, "could not create udev monitor\n");
        udev_monitor_unref(mon1);
        udev_unref(udev);
        return 1;
    }

    for (auto **p = notag_subsys; *p; ++p) {
        if (udev_monitor_filter_add_match_subsystem_devtype(mon1, *p, NULL) < 0) {
            std::fprintf(stderr, "could not set up monitor filter for '%s'\n", *p);
            udev_monitor_unref(mon1);
            udev_monitor_unref(mon2);
            udev_unref(udev);
            return 1;
        }
    }

    if (
        (udev_monitor_filter_add_match_tag(mon2, "systemd") < 0) ||
        (udev_monitor_filter_add_match_tag(mon2, "dinit") < 0)
    ) {
        std::fprintf(stderr, "could not set up udev monitor tag filters\n");
        udev_monitor_unref(mon1);
        udev_monitor_unref(mon2);
        udev_unref(udev);
        return 1;
    }

    if (
        (udev_monitor_enable_receiving(mon1) < 0) ||
        (udev_monitor_enable_receiving(mon2) < 0)
    ) {
        std::fprintf(stderr, "could not set enable udev monitor receiving\n");
        udev_monitor_unref(mon1);
        udev_monitor_unref(mon2);
        udev_unref(udev);
        return 1;
    }

    {
        auto &pfd1 = fds.emplace_back();
        pfd1.fd = udev_monitor_get_fd(mon1);
        pfd1.events = POLLIN;
        pfd1.revents = 0;

        auto &pfd2 = fds.emplace_back();
        pfd2.fd = udev_monitor_get_fd(mon2);
        pfd2.events = POLLIN;
        pfd2.revents = 0;
    }
#endif

    std::printf("devmon: main loop\n");

    int ret = 0;
    for (;;) {
        std::size_t ni = 0;
        std::printf("devmon: poll\n");
        auto pret = poll(fds.data(), fds.size(), -1);
        if (pret < 0) {
            if (errno == EINTR) {
                goto do_compact;
            }
            warn("poll failed");
            ret = 1;
            break;
        } else if (pret == 0) {
            goto do_compact;
        }
        /* signal fd */
        if (fds[ni].revents == POLLIN) {
            int sign;
            if (read(fds[ni].fd, &sign, sizeof(sign)) != sizeof(sign)) {
                warn("signal read failed");
                goto do_compact;
            }
            /* sigterm or sigint */
            break;
        }
        /* check for incoming connections */
        if (fds[++ni].revents) {
            for (;;) {
                auto afd = accept4(fds[ni].fd, nullptr, nullptr, SOCK_NONBLOCK);
                if (afd < 0) {
                    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                        warn("accept4 failed");
                    }
                    break;
                }
                auto &rfd = fds.emplace_back();
                rfd.fd = afd;
                rfd.events = POLLIN | POLLHUP;
                rfd.revents = 0;
                std::printf("devmon: accepted %d\n", afd);
            }
        }
        /* check on udev */
#ifdef HAVE_UDEV
        if (fds[++ni].revents && !resolve_device(mon1, false)) {
            ret = 1;
            break;
        }
        if (fds[++ni].revents && !resolve_device(mon2, true)) {
            ret = 1;
            break;
        }
#endif
        /* handle connections */
        for (std::size_t i = ni + 1; i < fds.size(); ++i) {
            conn *nc = nullptr;
            unsigned char igot;
            if (fds[i].revents == 0) {
                continue;
            }
            if (fds[i].revents & POLLHUP) {
                std::printf("devmon: term %d\n", fds[i].fd);
                goto bad_msg;
            }
            if (fds[i].revents & POLLIN) {
                /* look up if we already have a connection */
                for (auto &cnc: conns) {
                    if (cnc.fd == fds[i].fd) {
                        nc = &cnc;
                        break;
                    }
                }
                if (!nc) {
                    /* got none, make one */
                    nc = &conns.emplace_back();
                    nc->fd = fds[i].fd;
                } else {
                    /* if it's complete, we are not expecting any more...
                     * so any more stuff received is junk and we drop the
                     * connection just in case
                     */
                    if (nc->datalen && (nc->datastr.size() == nc->datalen)) {
                        warnx("devmon: received junk for %d", fds[i].fd);
                        goto bad_msg;
                    }
                }
                if (!nc->handshake[0]) {
                    /* ensure we read all 8 bytes */
                    if (read(
                        fds[i].fd, nc->handshake, sizeof(nc->handshake)
                    ) != sizeof(nc->handshake)) {
                        warnx("devmon: incomplete handshake for %d", fds[i].fd);
                        goto bad_msg;
                    }
                    /* ensure the message is good */
                    if (
                        (static_cast<unsigned char>(nc->handshake[0]) != 0xDD) ||
                        nc->handshake[sizeof(nc->handshake) - 1]
                    ) {
                        warnx("devmon: invalid handshake for %d", fds[i].fd);
                        goto bad_msg;
                    }
                    /* ensure the requested type is valid */
                    auto *msgt = &nc->handshake[1];
                    if (!std::strcmp(msgt, "dev")) {
                        nc->devtype = DEVICE_DEV;
                    } else if (!std::strcmp(msgt, "netif")) {
                        nc->devtype = DEVICE_NETIF;
                    } else if (!std::strcmp(msgt, "mac")) {
                        nc->devtype = DEVICE_MAC;
                    } else {
                        warnx(
                            "devmon: invalid requested type '%s' for %d",
                            msgt, fds[i].fd
                        );
                        goto bad_msg;
                    }
                    /* good msg, the rest is sent separately */
                    continue;
                }
                if (!nc->datalen) {
                    if ((read(
                        fds[i].fd, &nc->datalen, sizeof(nc->datalen)
                    ) != sizeof(nc->datalen)) || !nc->datalen) {
                        warnx("devmon: could not receive datalen for %d", fds[i].fd);
                        goto bad_msg;
                    }
                    /* good msg, proceed with reading the data */
                }
                /* don't read any extra - that's junk */
                if (nc->datastr.size() >= nc->datalen) {
                    warnx("devmon: received extra data for %d\n", fds[i].fd);
                    goto bad_msg;
                }
                /* read until stuff's full */
                while (nc->datastr.size() < nc->datalen) {
                    unsigned char c = 0;
                    errno = 0;
                    if (read(fds[i].fd, &c, sizeof(c)) != sizeof(c)) {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            break;
                        }
                        warn("read failed for %d", fds[i].fd);
                        goto bad_msg;
                    }
                    nc->datastr.push_back(char(c));
                }
                switch (nc->devtype) {
                    case DEVICE_DEV:
                        igot = check_devnode(nc->datastr) ? 1 : 0;
                        break;
                    case DEVICE_NETIF:
                        igot = (map_netif.find(nc->datastr) != map_netif.end()) ? 1 : 0;
                        break;
                    case DEVICE_MAC:
                        igot = (map_mac.find(nc->datastr) != map_mac.end()) ? 1 : 0;
                        break;
                    default:
                        /* should never happen */
                        warnx("devmon: invalid devtype for %d", fds[i].fd);
                        goto bad_msg;
                }
                std::printf(
                    "devmon: send status %d for %s for %d\n",
                    int(igot), nc->datastr.c_str(), fds[i].fd
                );
                if (write(fds[i].fd, &igot, sizeof(igot)) != sizeof(igot)) {
                    warn("write failed for %d\n", fds[i].fd);
                    goto bad_msg;
                }
                continue;
bad_msg:
                if (nc) {
                    for (auto it = conns.begin(); it != conns.end(); ++it) {
                        if (it->fd == nc->fd) {
                            conns.erase(it);
                            break;
                        }
                    }
                }
                close(fds[i].fd);
                fds[i].fd = -1;
                fds[i].revents = 0;
            }
        }
do_compact:
        if (ret) {
            break;
        }
        std::printf("devmon: loop compact\n");
        for (auto it = fds.begin(); it != fds.end();) {
            if (it->fd == -1) {
                it = fds.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = conns.begin(); it != conns.end();) {
            if (it->fd == -1) {
                it = conns.erase(it);
            } else {
                ++it;
            }
        }
    }
    /* close control socket and signal fd */
    close(fds[0].fd);
    close(fds[1].fd);
    /* close connections */
    for (auto &cnc: conns) {
        close(cnc.fd);
    }
#ifdef HAVE_UDEV
    /* clean up udev resources if necessary */
    udev_monitor_unref(mon1);
    udev_monitor_unref(mon2);
    udev_unref(udev);
#endif
    std::printf("devmon: exit with %d\n", ret);
    /* intended return code */
    return ret;
}
