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
#include <cctype>
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

#include <libdinitctl.h>

#ifdef HAVE_UDEV
#include <libudev.h>

/* subsystems we always match even without a tag */
static char const *notag_subsys[] = {
    "block",
    "net",
    "tty",
    "usb",
    nullptr
};
#endif

#ifndef DEVMON_SOCKET
#error monitor socket is not provided
#endif

enum {
    DEVICE_SYS = 1,
    DEVICE_DEV,
    DEVICE_NETIF,
    DEVICE_MAC,
    DEVICE_USB,
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

static bool check_devnode(
    std::string const &node, char const *devn = nullptr,
    std::string_view *syspath = nullptr
) {
    if (!devn) {
        auto it = map_dev.find(node);
        if (it != map_dev.end()) {
            if (syspath) {
                *syspath = it->second;
            }
            return true;
        }
    } else if (node == devn) {
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
    bool ret = false;
    if (!devn) {
        auto it = map_dev.find(respath);
        if (it != map_dev.end()) {
            if (syspath) {
                *syspath = it->second;
            }
            ret = true;
        }
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

static void write_gen(int devt, unsigned char igot, std::string const &name) {
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
    /* for usb devices, a set of real syspaths that share this */
    std::unordered_set<dev_t> devset;
    /* services that are currently dependencies and being dropped */
    std::unordered_set<std::string> dsvcset;
    /* services that are in process of becoming dependencies */
    std::unordered_set<std::string> psvcset;
    /* services that are pending and will become psvcset after that is cleared */
    std::unordered_set<std::string> nsvcset;
    dinitctl_service_handle *device_svc = nullptr;
    std::size_t pending_svcs = 0;
    /* device is most recently removed, regardless of event */
    bool removed = false;
    /* currently processing an event */
    bool processing = false;
    /* currently being-processed event is a removal */
    bool removal = false;
    /* there is an upcoming event pending */
    bool pending = false;
    /* device has or had a dinit/systemd tag at one point */
    bool has_tag = false;

    void init_dev(char const *node) {
        if (node) {
            name = node;
        }
        std::printf(
            "devmon: add %s '%s'\n", subsys.c_str(), name.c_str()
        );
        if (node) {
            map_dev.emplace(name, syspath);
        }
    }

    void init_net(char const *ifname, char const *macaddr) {
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
            map_netif.emplace(name, syspath);
        }
        if (macaddr) {
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
        write_gen(DEVICE_NETIF, 0, name);
        map_netif.erase(name);
        if (ifname) {
            name = ifname;
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
        write_gen(DEVICE_MAC, 0, mac);
        map_mac.erase(mac);
        if (nmac) {
            mac = nmac;
            map_mac.emplace(name, syspath);
        } else {
            mac.clear();
        }
    }

    void ready(unsigned char status) {
        std::printf("devmon: ready %d for '%s'\n", int(status), syspath.c_str());
        if (subsys == "usb") {
            write_gen(DEVICE_USB, status, syspath);
            /* we don't support syspaths */
            return;
        }
        write_gen(DEVICE_SYS, status, syspath);
        if (subsys == "net") {
            if (!name.empty()) {
                write_gen(DEVICE_NETIF, status, name);
            }
            if (!mac.empty()) {
                write_gen(DEVICE_MAC, status, mac);
            }
        } else {
            if (!name.empty()) {
                write_dev(status, name);
            }
        }
    }

#ifdef HAVE_UDEV
    void init(struct udev_device *dev, dev_t devnum) {
        if (devnum) {
            devset.emplace(devnum);
        } else if (subsys != "net") {
            init_dev(udev_device_get_devnode(dev));
        } else {
            init_net(
                udev_device_get_sysname(dev),
                udev_device_get_sysattr_value(dev, "address")
            );
        }
        removed = false;
    }

    void set(struct udev_device *dev, dev_t devnum) {
        if (devnum) {
            devset.emplace(devnum);
        } else if (subsys != "net") {
            set_dev(udev_device_get_devnode(dev));
        } else {
            set_ifname(udev_device_get_sysname(dev));
            set_mac(udev_device_get_sysattr_value(dev, "address"));
        }
        removed = false;
    }
#endif

    bool process(dinitctl *ctl);

    void remove() {
        if (subsys == "net") {
            std::printf(
                "devmon: drop netif '%s' (mac: '%s')\n",
                name.c_str(), mac.c_str()
            );
            if (!name.empty()) {
                map_netif.erase(name);
                name.clear();
            }
            if (!mac.empty()) {
                map_mac.erase(name);
                mac.clear();
            }
        } else {
            std::printf(
                "devmon: drop %s '%s'\n", subsys.c_str(), name.c_str()
            );
            if (!name.empty()) {
                map_dev.erase(name);
                name.clear();
            }
        }
    }
};

/* canonical mapping of syspath to devices, also holds the memory */
static std::unordered_map<std::string, device> map_sys;
static std::unordered_map<dev_t, device *> map_usb{};

/* service set */
static std::unordered_set<std::string> svc_set{};

#ifdef HAVE_UDEV
static struct udev *udev;
#endif

static dinitctl *dctl;
static dinitctl_service_handle *dinit_system;

static std::unordered_map<dinitctl_service_handle *, device *> map_svcdev;

static void sig_handler(int sign) {
    write(sigpipe[1], &sign, sizeof(sign));
}

static void handle_dinit_event(
    dinitctl *ctl, dinitctl_service_handle *handle,
    enum dinitctl_service_event, dinitctl_service_status const *, void *
) {
    auto it = map_svcdev.find(handle);
    if (it == map_svcdev.end()) {
        return;
    }
    device *dev = it->second;
    /* we don't care about the new status actually, just that it became it */
    if (!--dev->pending_svcs && !dev->process(ctl)) {
        dinitctl_abort(ctl, errno);
    }
    /* erase afterwards */
    map_svcdev.erase(it);
    /* and close the handle for this */
    auto close_cb = [](dinitctl *ictl, void *) {
        dinitctl_close_service_handle_finish(ictl);
    };
    if (dinitctl_close_service_handle_async(
        ctl, handle, close_cb, nullptr
    ) < 0) {
        dinitctl_abort(ctl, errno);
    }
}

/* service from a set has been loaded */
static void dinit_subsvc_load_cb_base(dinitctl *ctl, void *data, bool removal) {
    auto *dev = static_cast<device *>(data);
    dinitctl_service_handle *ish;
    dinitctl_service_state st;
    auto ret = dinitctl_load_service_finish(
        ctl, &ish, &st, nullptr
    );
    bool no_wake = false;
    if (ret < 0) {
        dinitctl_abort(ctl, errno);
        return;
    } else if (ret > 0) {
        /* could not load, don't worry about it anymore */
        if (!--dev->pending_svcs && !dev->process(ctl)) {
            dinitctl_abort(ctl, errno);
        }
        return;
    } else if (removal || st == DINITCTL_SERVICE_STATE_STARTED) {
        /* already started so we don't expect a service event, process here
         * that said, we still want to add the softdep, so don't return here!
         */
        no_wake = true;
    } else {
        /* keep track of it for the event */
        map_svcdev.emplace(ish, dev);
    }
    /* a "regular" callback that performs a wake */
    auto dep_cb = [](dinitctl *ictl, void *idata) {
        dinitctl_add_remove_service_dependency_finish(ictl);
        auto *iish = static_cast<dinitctl_service_handle *>(idata);
        auto wake_cb = [](dinitctl *jctl, void *) {
            dinitctl_wake_service_finish(jctl, nullptr);
        };
        /* give the service a wake once the dependency is either added or not,
         * just to ensure it gets started if the dependency already existed
         * or whatever... we want our event callback
         */
        if (dinitctl_wake_service_async(
            ictl, iish, false, false, wake_cb, nullptr
        ) < 0) {
            dinitctl_abort(ictl, errno);
        }
        /* we don't close the handle here because we expect an event callback */
    };
    /* one without a wake because the service was already started */
    auto dep_nowake_cb = [](dinitctl *ictl, void *idata) {
        dinitctl_add_remove_service_dependency_finish(ictl);
        auto *iish = static_cast<dinitctl_service_handle *>(idata);
        auto close_cb = [](dinitctl *jctl, void *) {
            dinitctl_close_service_handle_finish(jctl);
        };
        /* we close the handle here because no callback is expected */
        if (dinitctl_close_service_handle_async(
            ictl, iish, close_cb, nullptr
        ) < 0) {
            dinitctl_abort(ictl, errno);
        }
    };
    /* we don't care about if it already exists or whatever... */
    if (dinitctl_add_remove_service_dependency_async(
        ctl, dev->device_svc, ish, DINITCTL_DEPENDENCY_WAITS_FOR,
        removal, !removal, no_wake ? dep_nowake_cb : dep_cb, ish
    ) < 0) {
        dinitctl_abort(ctl, errno);
        return;
    }
    /* at the end if we don't do a wake, process and close */
    if (no_wake && !--dev->pending_svcs && !dev->process(ctl)) {
        dinitctl_abort(ctl, errno);
    }
}

/* version for services being dropped */
static void dinit_subsvc_load_del_cb(dinitctl *ctl, void *data) {
    dinit_subsvc_load_cb_base(ctl, data, true);
}

/* version for services being added */
static void dinit_subsvc_load_add_cb(dinitctl *ctl, void *data) {
    dinit_subsvc_load_cb_base(ctl, data, false);
}

/* dependency system => device@/sys/... was added/removed =>
 * if this was a removal, do nothing else, otherwise loop all the
 * services in the set and load each to prepare them to be added
 */
static void dinit_devsvc_add_cb(dinitctl *ctl, void *data) {
    auto *dev = static_cast<device *>(data);
    dinitctl_add_remove_service_dependency_finish(ctl);
    dev->pending_svcs = 0;
    /* now remove old deps if any */
    for (auto it = dev->dsvcset.begin(); it != dev->dsvcset.end(); ++it) {
        if (dinitctl_load_service_async(
            ctl, it->c_str(), true, dinit_subsvc_load_del_cb, dev
        ) < 0) {
            dinitctl_abort(ctl, errno);
            return;
        }
        ++dev->pending_svcs;
    }
    /* and add new ones */
    for (auto it = dev->psvcset.begin(); it != dev->psvcset.end(); ++it) {
        if (dinitctl_load_service_async(
            ctl, it->c_str(), false, dinit_subsvc_load_add_cb, dev
        ) < 0) {
            dinitctl_abort(ctl, errno);
            return;
        }
        ++dev->pending_svcs;
    }
}

/* device@/sys/... has been loaded =>
 * add the dependency from system to this service, enabling it,
 * alternatively remove the dependency causing all to stop
 */
static void dinit_devsvc_load_cb(dinitctl *ctl, void *data) {
    auto *dev = static_cast<device *>(data);
    dinitctl_service_handle *sh;
    auto ret = dinitctl_load_service_finish(ctl, &sh, nullptr, nullptr);
    dev->device_svc = sh;
    if (ret < 0) {
        dinitctl_abort(ctl, errno);
        return;
    } else if (ret > 0) {
        if (!dev->process(ctl)) {
            dinitctl_abort(ctl, errno);
        }
        return;
    }
    if (dinitctl_add_remove_service_dependency_async(
        ctl, dinit_system, sh, DINITCTL_DEPENDENCY_WAITS_FOR,
        dev->removal, !dev->removal, dinit_devsvc_add_cb, dev
    ) < 0) {
        dinitctl_abort(ctl, errno);
    }
}

bool device::process(dinitctl *ctl) {
    /* signal the prior readiness and close the handle if we have it */
    auto close_cb = [](dinitctl *ictl, void *) {
        dinitctl_close_service_handle_finish(ictl);
    };
    /* close the device handle... */
    if (device_svc && (dinitctl_close_service_handle_async(
        ctl, device_svc, close_cb, nullptr
    ) < 0)) {
        warn("could not close device service handle");
        processing = pending = false;
        return false;
    }
    device_svc = nullptr;
    /* signal the readiness to clients */
    ready(removal ? 0 : 1);
    /* shuffle the sets; previous current set becomes removal set */
    dsvcset = std::move(psvcset);
    /* and pending set becomes to-be-added set */
    psvcset = std::move(nsvcset);
    /* just so we can call this from anywhere */
    if (!pending) {
        processing = false;
        return true;
    }
    std::string dsvc = "device@";
    dsvc += syspath;
    pending = false;
    removal = removed;
    if (dinitctl_load_service_async(
        ctl, dsvc.c_str(), removed, dinit_devsvc_load_cb, this
    ) < 0) {
        warn("could not issue load_service");
        processing = false;
        return false;
    }
    processing = true;
    return true;
}

#ifdef HAVE_UDEV
static bool handle_device_dinit(struct udev_device *dev, device &devm) {
    /* if not formerly tagged, check if it's tagged now */
    if (!devm.has_tag) {
        devm.has_tag = udev_device_has_tag(dev, "dinit");
    }
    /* if never tagged, take the fast path */
    if (!devm.has_tag) {
        /* we can skip the service waits */
        devm.ready(devm.removed ? 0 : 1);
        return true;
    }
    char const *svcs = "";
    /* when removing, don't read the var, we don't care anyway */
    if (!devm.removed) {
        auto *usvc = udev_device_get_property_value(dev, "DINIT_WAITS_FOR");
        if (usvc) {
            svcs = usvc;
        }
    }
    /* add stuff to the set */
    devm.nsvcset.clear();
    for (;;) {
        while (std::isspace(*svcs)) {
            ++svcs;
        }
        auto *sep = svcs;
        while (*sep && !std::isspace(*sep)) {
            ++sep;
        }
        auto sv = std::string_view{svcs, std::size_t(sep - svcs)};
        if (sv.empty()) {
            /* no more */
            break;
        }
        devm.nsvcset.emplace(sv);
        svcs = sep;
    }
    /* we are not keeping a queue, so if multiple add/del events comes in while
     * we are still processing a previous one, only the latest will be processed
     * but that is probably fine, a harmless edge case
     */
    devm.pending = true;
    /* if not processing anything else at the moment, trigger it now,
     * otherwise it will be triggered by the previous operation at its end
     */
    if (!devm.processing && !devm.process(dctl)) {
        return false;
    }
    return true;
}

static bool add_device(
    struct udev_device *dev, char const *sysp, char const *ssys
) {
    std::string usbpath;
    dev_t devnum = 0;
    if (!std::strcmp(ssys, "usb")) {
        /* we don't support syspaths for usb devices... */
        auto *vendid = udev_device_get_sysattr_value(dev, "idVendor");
        auto *prodid = udev_device_get_sysattr_value(dev, "idProduct");
        if (!vendid || !prodid) {
            /* don't add devices without a clear id at all... */
            return true;
        }
        /* construct a match id */
        usbpath = vendid;
        usbpath.push_back(':');
        usbpath.append(prodid);
        sysp = usbpath.c_str();
        devnum = udev_device_get_devnum(dev);
    }
    auto odev = map_sys.find(sysp);
    if ((odev != map_sys.end()) && !odev->second.removed) {
        /* preexisting entry */
        odev->second.set(dev, devnum);
        if (!handle_device_dinit(dev, odev->second)) {
            return false;
        }
        return true;
    }
    /* new entry */
    auto &devm = map_sys[sysp];
    devm.syspath = sysp;
    devm.subsys = ssys;
    devm.init(dev, devnum);
    if (devnum) {
        map_usb[devnum] = &devm;
    }
    if (!handle_device_dinit(dev, devm)) {
        return false;
    }
    return true;
}

static bool remove_device(struct udev_device *dev, char const *sysp) {
    auto devn = udev_device_get_devnum(dev);
    if (devn) {
        auto dit = map_usb.find(devn);
        if (dit != map_usb.end()) {
            auto &dev = *(dit->second);
            /* the match id */
            sysp = dev.syspath.c_str();
            /* remove the device from the registered set and drop the mapping */
            dev.devset.erase(devn);
            map_usb.erase(dit);
            /* if there are still devices with this match id, bail */
            if (!dev.devset.empty()) {
                return true;
            }
        } else {
            /* not usb */
        }
    }
    auto it = map_sys.find(sysp);
    if ((it == map_sys.end()) || it->second.removed) {
        return true;
    }
    auto &devm = it->second;
    devm.removed = true;
    if (!handle_device_dinit(dev, devm)) {
        return false;
    }
    devm.remove();
    return true;
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
        if (!add_device(dev, path, udev_device_get_subsystem(dev))) {
            udev_enumerate_unref(en);
            return false;
        }
    }
    return true;
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
    auto *act = udev_device_get_action(dev);
    if (!std::strcmp(act, "bind") || !std::strcmp(act, "unbind")) {
        /* we don't care about these actions */
        udev_device_unref(dev);
        return true;
    }
    bool rem = !std::strcmp(act, "remove");
    std::printf("devmon: %s device '%s'\n", rem ? "drop" : "add", sysp);
    bool ret;
    if (rem) {
        ret = remove_device(dev, sysp);
    } else {
        ret = add_device(dev, sysp, ssys);
    }
    udev_device_unref(dev);
    return ret;
}
#endif

int main(int argc, char **argv) {
    if (argc > 2) {
        errx(1, "usage: %s [fd]", argv[0]);
    }

#ifdef HAVE_UDEV
    bool dummy_mode = false;
#else
    bool dummy_mode = true;
#endif
    if (std::getenv("DINIT_DEVMON_DUMMY_MODE")) {
        dummy_mode = true;
    } else {
        auto *cont = std::getenv("DINIT_CONTAINER");
        if (cont && !std::strcmp(cont, "1")) {
            dummy_mode = true;
        } else if (!access("/run/dinit/container", R_OK)) {
            dummy_mode = true;
        }
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

    /* readiness as soon as we're bound to a socket */
    if (fdnum > 0) {
        std::printf("devmon: readiness notification\n");
        write(fdnum, "READY=1\n", sizeof("READY=1"));
        close(fdnum);
    }

    std::printf("devmon: init dinit\n");
    /* set up dinit control connection */
    auto *denv = std::getenv("DINIT_CS_FD");
    if (denv) {
        auto dfd = atoi(denv);
        if (!dfd || (fcntl(dfd, F_GETFD) < 0)) {
            std::fprintf(stderr, "dinit control fd is not a file descriptor\n");
            return 1;
        }
        dctl = dinitctl_open_fd(dfd);
    } else {
        dctl = dinitctl_open_system();
    }
    if (!dctl) {
        warn("failed to set up dinitctl");
        return 1;
    }

    char const *sserv = std::getenv("DINIT_SYSTEM_SERVICE");
    if (!sserv || !*sserv) {
        sserv = "system";
    }
    std::printf("devmon: locate service '%s'\n", sserv);
    /* get a permanent handle to the service we'll depend on */
    if (dinitctl_load_service(
        dctl, sserv, true, &dinit_system, nullptr, nullptr
    ) != 0) {
        std::fprintf(stderr, "could not get a handle to the dinit system service");
        return 1;
    }

    if (dinitctl_set_service_event_callback(
        dctl, handle_dinit_event, nullptr
    ) < 0) {
        warn("failed to set up dinitctl event callback");
        return 1;
    }

#ifdef HAVE_UDEV
    struct udev_enumerate *en1, *en2;
    struct udev_monitor *mon1, *mon2;

    if (dummy_mode) {
        goto udev_inited;
    }

    std::printf("devmon: udev init\n");
    udev = udev_new();
    if (!udev) {
        std::fprintf(stderr, "could not create udev\n");
        return 1;
    }

    /* prepopulate the mappings */
    en1 = udev_enumerate_new(udev);
    en2 = udev_enumerate_new(udev);

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

    mon1 = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon1) {
        std::fprintf(stderr, "could not create udev monitor\n");
        udev_unref(udev);
        return 1;
    }

    mon2 = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon2) {
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

    if (!initial_populate(en1) || !initial_populate(en2)) {
        udev_enumerate_unref(en1);
        udev_enumerate_unref(en2);
        udev_unref(udev);
        return 1;
    }

    udev_enumerate_unref(en1);
    udev_enumerate_unref(en2);

    {
        auto &pfd1 = fds.emplace_back();
        pfd1.fd = udev_monitor_get_fd(mon1);
        pfd1.events = POLLIN;
        pfd1.revents = 0;

        auto &pfd2 = fds.emplace_back();
        pfd2.fd = udev_monitor_get_fd(mon2);
        pfd2.events = POLLIN;
        pfd2.revents = 0;

        auto &pfd3 = fds.emplace_back();
        pfd3.fd = dinitctl_get_fd(dctl);
        pfd3.events = POLLIN | POLLHUP;
        pfd3.revents = 0;
    }
#endif

udev_inited:
    /* dispatch pending dinit events */
    std::printf("devmon: drain dinit write queue\n");
    for (;;) {
        auto nev = dinitctl_dispatch(dctl, 0, nullptr);
        if (nev < 0) {
            if (errno == EINTR) {
                continue;
            }
            warn("dinitctl_dispatch failed");
            return 1;
        } else if (!nev) {
            break;
        }
    }

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
        if (!dummy_mode) {
            if (fds[++ni].revents && !resolve_device(mon1, false)) {
                ret = 1;
                break;
            }
            if (fds[++ni].revents && !resolve_device(mon2, true)) {
                ret = 1;
                break;
            }
        }
#endif
        /* we don't check fd revents here; we need to dispatch anyway
         * to send out any requests that may be in the write buffer
         * from e.g. udev monitor events
         */
        ++ni; /* skip over the dinit fd */
        for (;;) {
            auto nev = dinitctl_dispatch(dctl, 0, nullptr);
            if (nev < 0) {
                if (errno == EINTR) {
                    continue;
                }
                warn("dinitctl_dispatch failed");
                ret = 1;
                goto do_compact;
            } else if (!nev) {
                break;
            }
        }
        /* handle connections */
        for (std::size_t i = ni + 1; i < fds.size(); ++i) {
            conn *nc = nullptr;
            unsigned char igot;
            std::string_view syspath;
            if (fds[i].revents == 0) {
                continue;
            }
            if (fds[i].revents & POLLHUP) {
                std::printf("devmon: term %d\n", fds[i].fd);
                /* look up the connection so we can nuke it */
                for (auto &cnc: conns) {
                    if (cnc.fd == fds[i].fd) {
                        nc = &cnc;
                        break;
                    }
                }
                /* now terminate */
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
                    } else if (!std::strcmp(msgt, "sys")) {
                        nc->devtype = DEVICE_SYS;
                    } else if (!std::strcmp(msgt, "netif")) {
                        nc->devtype = DEVICE_NETIF;
                    } else if (!std::strcmp(msgt, "mac")) {
                        nc->devtype = DEVICE_MAC;
                    } else if (!std::strcmp(msgt, "usb")) {
                        nc->devtype = DEVICE_USB;
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
                igot = 0;
                switch (nc->devtype) {
                    case DEVICE_DEV:
                        if (check_devnode(nc->datastr, nullptr, &syspath)) {
                            igot = 1;
                        }
                        break;
                    case DEVICE_SYS:
                    case DEVICE_USB:
                        syspath = nc->datastr;
                        if (map_sys.find(nc->datastr) != map_sys.end()) {
                            igot = 1;
                        }
                        break;
                    case DEVICE_NETIF: {
                        auto it = map_netif.find(nc->datastr);
                        if (it != map_netif.end()) {
                            syspath = it->second;
                            igot = 1;
                        }
                        break;
                    }
                    case DEVICE_MAC: {
                        auto it = map_mac.find(nc->datastr);
                        if (it != map_mac.end()) {
                            syspath = it->second;
                            igot = 1;
                        }
                        break;
                        break;
                    }
                    default:
                        /* should never happen */
                        warnx("devmon: invalid devtype for %d", fds[i].fd);
                        goto bad_msg;
                }
                if (igot) {
                    /* perform a syspath lookup and see if it's really ready */
                    auto &dev = map_sys.at(std::string{syspath});
                    if (dev.removed || dev.processing) {
                        /* removed means we need 0 anyway, and processing means
                         * the current event is done yet so we will signal it
                         * later for proper waits-for behavior
                         */
                        igot = 0;
                    }
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
    dinitctl_close(dctl);
    std::printf("devmon: exit with %d\n", ret);
    /* intended return code */
    return ret;
}
