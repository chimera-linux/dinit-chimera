/*
 * Kernel module helper program
 *
 * This utility facilitates kernel module handling during early boot, having
 * more flexibility than modprobe and similar, and notably being able to deal
 * with modules-load.d.
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

#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <vector>
#include <string>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>

#include <err.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/utsname.h>

#include <libkmod.h>

static std::unordered_set<std::string_view> *kernel_blacklist = nullptr;

/* search paths for conf files */
static char const *paths[] = {
    "/etc/modules-load.d",
    "/run/modules-load.d",
    "/usr/local/lib/modules-load.d",
    "/usr/lib/modules-load.d",
    nullptr
};

static void usage(FILE *f) {
    extern char const *__progname;
    std::fprintf(f, "Usage: %s command [arg]\n"
"\n"
"Kernel module helper tool.\n"
"\n"
"Commands:\n"
"  static-modules  Load early static kernel modules.\n"
"  modules         Load modules specified in modules-load.d.\n"
"  load MODNAME    Load the module MODNAME.\n",
        __progname
    );
}

static bool mod_is_kernel_blacklist(char const *modname) {
    return (kernel_blacklist->find(modname) != kernel_blacklist->end());
}

static int mod_load(struct kmod_ctx *ctx, char const *modname) {
    struct kmod_list *modlist = nullptr;
    struct kmod_list *it;
    /* first lookup the list */
    int ret = kmod_module_new_from_lookup(ctx, modname, &modlist);
    if (ret < 0) {
        return ret;
    }
    /* missing modules are a success */
    if (!modlist) {
        return 0;
    }
    /* otherwise we got a list, go over it */
    kmod_list_foreach(it, modlist) {
        struct kmod_module *km = kmod_module_get_module(it);
        int state = kmod_module_get_initstate(km);
        /* already-loaded or builtin modules are skipped */
        switch (state) {
            case KMOD_MODULE_BUILTIN:
            case KMOD_MODULE_LIVE:
                kmod_module_unref(km);
                continue;
            default:
                break;
        }
        /* actually perform a load */
        int r = kmod_module_probe_insert_module(
            km, KMOD_PROBE_APPLY_BLACKLIST, nullptr, nullptr, nullptr, nullptr
        );
        if (!r || (r == KMOD_PROBE_APPLY_BLACKLIST)) {
            continue;
        }
        /* handle kernel module_blacklist as libkmod does not handle it */
        if ((r == -EPERM) && mod_is_kernel_blacklist(modname)) {
            continue;
        }
        /* other "success" conditions */
        if ((r == -ENODEV) || (r == -ENOENT)) {
            continue;
        }
        /* else error but still move on, do try to probe everything first */
        ret = r;
    }
    /* ok */
    return ret;
}

static bool load_conf(
    struct kmod_ctx *ctx, char const *s, char *&line, std::size_t &len
) {
    FILE *f = std::fopen(s, "rb");
    if (!f) {
        warnx("could not load '%s'", s);
        return false;
    }
    bool fret = true;
    for (ssize_t nread; (nread = getline(&line, &len, f)) != -1;) {
        /* strip leading whitespace and ignore comments, empty lines etc */
        char *cline = line;
        while (std::isspace(*cline)) {
            ++cline;
        }
        if ((*cline == '#') || (*cline == ';') || !*cline) {
            continue;
        }
        /* strip trailing whitespace too once we are sure it's not empty */
        auto rl = std::strlen(line);
        while (std::isspace(line[rl - 1])) {
            line[--rl] = '\0';
        }
        /* try loading the module */
        if (mod_load(ctx, line) < 0) {
            warn("failed to load module '%s'", line);
            fret = false;
        }
    }
    std::fclose(f);
    return fret;
}

static int do_static_modules(struct kmod_ctx *ctx) {
    char buf[256], *bufp;
    int modb = open("/lib/modules", O_DIRECTORY | O_PATH);
    if (modb < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        warn("opening /lib/modules failed");
        return 2;
    }
    struct utsname ub;
    if (uname(&ub) < 0) {
        warn("uname");
        close(modb);
        return 2;
    }
    int kernb = openat(modb, ub.release, O_DIRECTORY | O_PATH);
    if (kernb < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        warn("opening kernel directory failed");
        close(modb);
        return 2;
    }
    close(modb);
    int devf = openat(kernb, "modules.devname", O_RDONLY);
    if (devf < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        warn("opening modules.devname failed");
        close(kernb);
        return 2;
    }
    FILE *df = fdopen(devf, "rb");
    if (!df) {
        warn("could not reopen modules.devname as file stream");
        close(devf);
        return 2;
    }
    while ((bufp = std::fgets(buf, sizeof(buf), df))) {
        auto sl = std::strlen(bufp);
        /* extract the module name */
        char *sp = std::strchr(bufp, ' ');
        if (sp) {
            *sp = '\0';
        }
        /* skip comments */
        if (bufp[0] != '#') {
            if (mod_load(ctx, bufp) < 0) {
                /* we don't want early-modules to fail if possible,
                 * but an error message is nice so display it anyway
                 */
                warn("failed to load module '%s'", bufp);
            }
        }
        /* exhaust the rest of the line just in case */
        while (bufp[sl - 1] != '\n') {
            bufp = std::fgets(buf, sizeof(buf), df);
            if (!bufp) {
                break;
            }
            sl = std::strlen(bufp);
        }
        /* bail early if we exhausted all without another fgets */
        if (!bufp) {
            break;
        }
    }
    return 0;
}

static int do_load(struct kmod_ctx *ctx, char const *modname) {
    if (mod_load(ctx, modname) < 0) {
        warn("failed to load module '%s'", modname);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv) {
    bool is_static_mods = false;
    bool is_load = false;

    if (argc <= 1) {
        usage(stderr);
        return 1;
    }

    if (!std::strcmp(argv[1], "static-modules")) {
        is_static_mods = true;
    } else if (!std::strcmp(argv[1], "modules")) {
        /* implicit */
    } else if (!std::strcmp(argv[1], "load")) {
        is_load = true;
    } else {
        usage(stderr);
        return 1;
    }

    /* needs an argument */
    if (is_load && (argc <= 2)) {
        usage(stderr);
        return 1;
    }

    if ((access("/proc/modules", F_OK) < 0) && (errno == ENOENT)) {
        /* kernel not modular, all succeeds */
        return 0;
    }

    std::unordered_map<std::string, std::string> got_map;
    std::unordered_set<std::string_view> kern_bl;
    std::vector<std::string const *> ord_list;
    std::vector<char const *> cmdl_mods;
    char *line = nullptr;
    char *cmdp = nullptr;
    std::size_t len = 0;
    int ret = 0;

    kernel_blacklist = &kern_bl;

    struct kmod_ctx *kctx = kmod_new(nullptr, nullptr);
    if (!kctx) {
        err(1, "kmod_new");
    }

    kmod_load_resources(kctx);

    /* modules_load, modules-load, module_blacklist */
    FILE *cmdl = std::fopen("/proc/cmdline", "rb");
    if (cmdl) {
        std::fseek(cmdl, 0, SEEK_END);
        auto fs = std::ftell(cmdl);
        std::fseek(cmdl, 0, SEEK_SET);
        cmdp = static_cast<char *>(std::malloc(fs + 1));
        cmdp[fs] = '\0';
        if (long(std::fread(cmdp, 1, fs, cmdl)) != fs) {
            std::free(cmdp);
            err(1, "fread");
        }
        for (char *p = cmdp; (p = std::strstr(p, "module"));) {
            /* inside of a param, skip */
            if ((p != cmdp) && p[-1] && (p[-1] != ' ')) {
                p += 6;
                continue;
            }
            /* find a = */
            char *e = std::strpbrk(p, "= ");
            /* no useful data anymore */
            if (!e) {
                break;
            }
            /* located end earlier */
            if (*e == ' ') {
                p = e + 1;
                continue;
            }
            bool load = false;
            if (
                !std::strncmp(p, "modules_load", e - p) ||
                !std::strncmp(p, "modules-load", e - p)
            ) {
                load = true;
            } else if (std::strncmp(p, "module_blacklist", e - p)) {
                /* invalid */
                p = e + 1;
                continue;
            }
            /* now parse the list after e */
            p = e + 1;
            for (;;) {
                auto w = std::strcspn(p, ", ");
                if (!w) {
                    /* maybe had a trailing comma */
                    break;
                }
                char c = p[w];
                p[w] = '\0';
                if (load) {
                    cmdl_mods.push_back(p);
                } else {
                    kernel_blacklist->emplace(p);
                }
                if (c == ',') {
                    /* the list continues, move past the comma */
                    p += w + 1;
                    continue;
                } else if (c == ' ') {
                    /* the list ends, move past the space */
                    p += w + 1;
                    break;
                }
                /* everything ends */
                p += w;
                break;
            }
        }
        return 0;
    }

    if (is_static_mods) {
        ret = do_static_modules(kctx);
        goto do_ret;
    } else if (is_load) {
        ret = do_load(kctx, argv[2]);
        goto do_ret;
    }

    for (char const **p = paths; *p; ++p) {
        DIR *dfd = opendir(*p);
        if (!dfd) {
            continue;
        }
        struct dirent *dp;
        while ((dp = readdir(dfd))) {
            /* must be a regular file */
            if (dp->d_type != DT_REG) {
                continue;
            }
            /* check if it matches .conf */
            char const *dn = dp->d_name;
            auto sl = std::strlen(dn);
            if ((sl <= 5) || strcmp(dn + sl - 5, ".conf")) {
                continue;
            }
            /* check if already in map */
            if (got_map.find(dn) != got_map.end()) {
                continue;
            }
            /* otherwise use its full name */
            std::string fp = *p;
            fp.push_back('/');
            fp += dp->d_name;
            got_map.emplace(dn, std::move(fp));
        }
        closedir(dfd);
    }

    /* construct a sorted vector of names, backed by map memory */
    for (auto &p: got_map) {
        ord_list.push_back(&p.first);
    }
    std::sort(ord_list.begin(), ord_list.end(), [](auto a, auto b) {
        return (*a < *b);
    });

    /* load modules from command line */
    for (auto modn: cmdl_mods) {
        if (do_load(kctx, modn)) {
            ret = 2;
        }
    }
    /* now register or print each conf */
    for (auto &c: ord_list) {
        if (!load_conf(kctx, got_map[*c].data(), line, len)) {
            ret = 2;
        }
    }
do_ret:
    std::free(line);
    std::free(cmdp);
    if (kctx) {
        kmod_unref(kctx);
    }
    return ret;
}
