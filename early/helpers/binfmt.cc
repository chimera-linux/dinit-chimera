/*
 * Binfmt setup helper program
 *
 * This is a utility that registers binfmt handlers using configuration files
 * compatible with the systemd-binfmt layout. It supports roughly the same
 * options as systemd-binfmt, but exists primarily for the service.
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

#include <unordered_map>
#include <algorithm>
#include <vector>
#include <string>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/vfs.h>
#include <sys/stat.h>

#ifndef BINFMTFS_MAGIC
/* from linux/magic.h */
#define BINFMTFS_MAGIC 0x42494e4d
#endif

/* /proc/sys/fs/binfmt_misc */
static int binfmt_fd = -1;

/* search paths for conf files */
static char const *paths[] = {
    "/etc/binfmt.d",
    "/usr/local/lib/binfmt.d",
    "/usr/lib/binfmt.d",
    nullptr
};

static void usage(FILE *f) {
    extern char const *__progname;
    std::fprintf(f, "Usage: %s [OPTION]...\n"
"\n"
"Register or unregister formats with binfmt_misc.\n"
"\n"
"      -u  Unregister instead of registering.\n"
"      -p  Print the contents of config files to standard output.\n"
"      -h  Print this message and exit.\n",
        __progname
    );
}

static void binfmt_check_mounted(bool print_only) {
    if (print_only) {
        return;
    }
    int fd = open("/proc/sys/fs/binfmt_misc", O_DIRECTORY | O_PATH);
    if (fd < 0) {
        err(1, "failed to open binfmt_misc");
    }
    /* check the magic */
    struct statfs buf;
    int ret = fstatfs(fd, &buf);
    if ((ret < 0) || (buf.f_type != BINFMTFS_MAGIC)) {
        err(1, "binfmt_misc has a wrong type");
    }
    /* check if it's writable */
    char proc[256];
    std::snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
    if (access(proc, W_OK) < 0) {
        err(1, "binfmt_misc is not writable");
    }
    /* now we good; O_PATH descriptor can be used with *at */
    binfmt_fd = fd;
}

static bool poke_bfmt(char const *path, char const *value, std::size_t vlen) {
    int fd = openat(binfmt_fd, path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        return false;
    }
    bool ret = (write(fd, value, vlen) == ssize_t(vlen));
    close(fd);
    return ret;
}

static bool load_rule(char *rule, std::size_t rlen) {
    /* get the name */
    char *rulename = rule + 1;
    char delim[2] = {rule[0], '\0'};
    /* length of name in rule */
    auto rulelen = std::strcspn(rulename, delim);
    /* validate */
    if (!rulelen) {
        warnx("invalid binfmt '%s'", rule);
        return false;
    }
    if (
        !std::strncmp(rulename, "register", rulelen) ||
        !std::strncmp(rulename, "status", rulelen) ||
        !std::strncmp(rulename, "..", rulelen) ||
        !std::strncmp(rulename, ".", rulelen) ||
        std::memchr(rulename, '/', rulelen)
    ) {
        warnx("invalid rule name in '%s'", rule);
        return false;
    }
    /* deregister old rule */
    rulename[rulelen] = '\0';
    if (!poke_bfmt(rulename, "-1", 2) && (errno != ENOENT)) {
        warn("failed to unregister rule '%s'", rulename);
        return false;
    }
    rulename[rulelen] = rule[0];
    /* register new rule */
    if (!poke_bfmt("register", rule, rlen)) {
        warn("failed to register rule '%s'", rule);
        return false;
    }
    /* success! */
    return true;
}

static bool load_conf(char const *s, char *&line, std::size_t &len) {
    FILE *f = std::fopen(s, "rb");
    if (!f) {
        warnx("could not load '%s'", s);
        return false;
    }
    bool fret = true;
    for (ssize_t nread; (nread = getline(&line, &len, f)) != -1;) {
        /* strip leading whitespace and ignore comments, empty lines etc */
        char *cline = line;
        auto rlen = std::size_t(nread);
        while (std::isspace(*cline)) {
            ++cline;
            --rlen;
        }
        if ((*cline == '#') || (*cline == ';') || !*cline) {
            continue;
        }
        /* strip trailing whitespace too once we are sure it's not empty */
        auto rl = std::strlen(line);
        while (std::isspace(line[rl - 1])) {
            line[--rl] = '\0';
            --rlen;
        }
        /* this should be a registerable binfmt */
        if (!load_rule(cline, rlen)) {
            fret = false;
        }
    }
    std::fclose(f);
    return fret;
}

static bool print_conf(char const *s, char *&line, std::size_t &len) {
    FILE *f = std::fopen(s, "rb");
    if (!f) {
        std::printf("# '%s' could not be loaded\n", s);
        return false;
    }
    std::printf("# %s\n", s);
    ssize_t nread;
    while ((nread = getline(&line, &len, f)) != -1) {
        std::printf("%s", line);
        if (line[nread - 1] != '\n') {
            /* just in case file is not terminated with newline */
            std::putchar('\n');
        }
    }
    std::fclose(f);
    return true;
}

static bool process_conf(
    char const *s, char *&line, std::size_t &len, bool only_print
) {
    if (only_print) {
        return print_conf(s, line, len);
    }
    return load_conf(s, line, len);
}

int main(int argc, char **argv) {
    bool arg_p = false;
    bool arg_u = false;

    for (int c; (c = getopt(argc, argv, "hpu")) >= 0;) {
        switch (c) {
            case 'h':
                usage(stdout);
                return 0;
            case 'p':
                arg_p = true;
                break;
            case 'u':
                arg_u = true;
                break;
            default:
                warnx("invalid option -- '%c'", c);
                usage(stderr);
                return 1;
        }
    }

    if (argc > optind) {
        warnx("extra arguments are not allowed");
        usage(stderr);
        return 1;
    }

    binfmt_check_mounted(arg_p);

    if (arg_u) {
        if (!poke_bfmt("status", "-1", 2)) {
            err(1, "failed to unregister binfmt entries");
        }
        /* success */
        return 0;
    }

    std::unordered_map<std::string, std::string> got_map;

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

    std::vector<std::string const *> ord_list;

    /* construct a sorted vector of names, backed by map memory */
    for (auto &p: got_map) {
        ord_list.push_back(&p.first);
    }
    std::sort(ord_list.begin(), ord_list.end(), [](auto a, auto b) {
        return (*a < *b);
    });

    int ret = 0;

    /* now register or print each conf */
    char *line = nullptr;
    std::size_t len = 0;
    for (auto &c: ord_list) {
        if (!process_conf(got_map[*c].data(), line, len, arg_p)) {
            ret = 1;
        }
    }
    std::free(line);
    close(binfmt_fd);
    return ret;
}
