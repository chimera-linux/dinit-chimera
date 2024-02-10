/*
 * Sysctl setup helper program
 *
 * This utility reads sysctl configuration files in the right order,
 * ensuring the behavior of procps's `sysctl --system`.
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

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

#include <err.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

/* /proc/sys */
static int sysctl_fd = -1;

/* search paths for conf files */
static char const *paths[] = {
    "/etc/sysctl.d",
    "/run/sysctl.d",
    "/usr/local/lib/sysctl.d",
    "/usr/lib/sysctl.d",
    nullptr
};

static void usage(FILE *f) {
    extern char const *__progname;
    std::fprintf(f, "Usage: %s\n"
"\n"
"Load sysctl settings.\n",
        __progname
    );
}

static bool load_sysctl(char *name, char *value, bool opt) {
    /* first, replace dots with slashes to get a path; we cannot just
     * replace all dots because e.g. foo/bar.baz/xyz can be a valid path,
     * so instead try names from the end, repeating with dots replaced with
     * slashes from the beginning until we've found the final correct value
     */
    char *nend = name + std::strlen(name);
    char *ntry = name - 1;
    for (*nend = '.'; *nend; *nend = '\0') {
rep:
        char *nptr = nend;
        for (; nptr > ntry; --nptr) {
            if (*nptr != '.') {
                continue;
            }
            *nptr = '\0';
            if (!faccessat(sysctl_fd, name, F_OK, AT_SYMLINK_NOFOLLOW)) {
                *nptr = '/';
                ntry = nptr;
                goto rep;
            }
            *nptr = '.';
        }
    }
    /* we have a valid pathname, so apply the sysctl */
    int fd = openat(sysctl_fd, name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        /* write-only values, we should not fail on those */
        if (errno == EACCES) {
            return true;
        }
        /* optional stuff never fails anyhow */
        if (opt) {
            return true;
        }
        /* unknown entries */
        if (errno == ENOENT) {
            for (;;) {
                char *sl = std::strchr(name, '/');
                if (!sl) {
                    break;
                }
                *sl = '.';
            }
            warnx("unknown sysctl '%s'", name);
            return false;
        }
        /* other error */
        warn("failed to set sysctl '%s'", name);
        return false;
    }
    auto vlen = std::strlen(value);
    value[vlen] = '\n';
    if ((write(fd, value, vlen + 1) != ssize_t(vlen + 1)) && !opt) {
        warn("failed to set sysctl '%s'", name);
        return false;
    }
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
        while (std::isspace(*cline)) {
            ++cline;
        }
        if ((*cline == '#') || (*cline == ';') || !*cline) {
            continue;
        }
        /* sysctls starting with - should not fail ever */
        bool opt = (*cline == '-');
        if (opt) {
            /* disregard the dash once we know, it's not a part of the name */
            ++cline;
        }
        /* strip trailing whitespace too once we are sure it's not empty */
        auto rl = std::strlen(line);
        while (std::isspace(line[rl - 1])) {
            line[--rl] = '\0';
        }
        /* find delimiter */
        auto *delim = std::strchr(cline, '=');
        if (!delim) {
            warnx("invalid sysctl: '%s'", cline);
            fret = false;
            continue;
        }
        *delim = '\0';
        /* split name and value, strip any excess whitespace */
        char *sname = cline;
        char *svalue = delim + 1;
        auto nl = std::strlen(sname);
        /* trailing spaces of name */
        while ((nl > 0) && std::isspace(sname[nl - 1])) {
            sname[--nl] = '\0';
        }
        if (!nl) {
            warnx("unnamed sysctl found");
            fret = false;
            continue;
        }
        /* leading spaces of value */
        while (std::isspace(*svalue)) {
            ++svalue;
        }
        /* load the sysctl */
        if (!load_sysctl(sname, svalue, opt)) {
            fret = false;
        }
    }
    std::fclose(f);
    return fret;
}

int main(int argc, char **) {
    if (argc != 1) {
        usage(stderr);
        return 1;
    }

    sysctl_fd = open("/proc/sys", O_DIRECTORY | O_PATH);
    if (sysctl_fd < 0) {
        err(1, "failed to open sysctl path");
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
        if (!load_conf(got_map[*c].data(), line, len)) {
            ret = 1;
        }
    }
    std::free(line);
    close(sysctl_fd);
    return ret;
}
