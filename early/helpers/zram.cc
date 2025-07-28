/*
 * Zram setup helper program
 *
 * This utility reads zram configuration files in the right order.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 q66 <q66@chimera-linux.org>
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
#include <unordered_set>
#include <algorithm>

#include <err.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* search paths for conf files */
static char const *paths[] = {
    "/etc/dinit-zram.d",
    "/run/dinit-zram.d",
    "/usr/local/lib/dinit-zram.d",
    "/usr/lib/dinit-zram.d",
    nullptr
};
static char const *sys_path = "/etc/dinit-zram.conf";

static void usage(FILE *f) {
    extern char const *__progname;
    std::fprintf(f, "Usage: %s zramN [config]\n"
"\n"
"Set up a zram device.\n",
        __progname
    );
}

static std::string zram_size{};
static std::string zram_algo{};
static std::string zram_algo_params{};
static std::string zram_mem_limit{};
static std::string zram_backing_dev{};
static std::string zram_writeback_limit{};
static std::string zram_fmt = "mkswap -U clear %0";

static bool write_param(
    int fd, char const *zdev, char const *file, char const *value
) {
    if (file) {
        fd = openat(fd, file, O_WRONLY);
        if (fd < 0) {
            warn("could not open '/sys/block/%s/reset'", zdev);
            return false;
        }
    }
    auto wn = write(fd, value, std::strlen(value));
    if (wn < 0) {
        warn("could not write '%s' to '%s' on '%s'", value, file, zdev);
        if (file) {
            close(fd);
        }
        return false;
    }
    return true;
}

static int zram_format(char const *zdevn) {
    /* prepare command */
    std::vector<char *> args;
    std::string zdev = "/dev/";
    zdev += zdevn;
    char *data = zram_fmt.data();
    /* strip any spaces at the beginning */
    while (std::isspace(*data)) {
        ++data;
    }
    for (;;) {
        auto sp = std::strchr(data, ' ');
        if (sp) {
            *sp = '\0';
        }
        if (!std::strcmp(data, "%0")) {
            args.push_back(zdev.data());
        } else {
            args.push_back(data);
        }
        if (!sp) {
            break;
        }
        data = sp + 1;
    }
    /* terminate */
    args.push_back(nullptr);
    /* and run */
    auto pid = fork();
    if (pid < 0) {
        warn("fork failed");
        return 1;
    } else if (pid == 0) {
        /* child */
        execvp(args[0], args.data());
        warn("exec failed");
        return 1;
    }
    /* parent */
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    if (WIFEXITED(st)) {
        st = WEXITSTATUS(st);
        if (st) {
            warnx("format comamnd '%s' exited with status %d", args[0]);
        }
        return st;
    } else if (WIFSIGNALED(st)) {
        warnx("format command '%s' killed by signal %d", WTERMSIG(st));
    } else if (WIFSTOPPED(st)) {
        warnx("format command '%s' stopped by signal %d", WSTOPSIG(st));
    }
    warnx("format command '%s' ended with unknown status");
    return 1;
}

static int setup_zram(char const *zdev, int znum) {
    if (zram_size.empty()) {
        warnx("no size specified for '%s'", zdev);
        return 1;
    }
    std::printf(
        "setting up device '%s' with size %s...\n", zdev, zram_size.data()
    );
    auto dev_fd = open("/dev", O_DIRECTORY | O_PATH);
    if (dev_fd < 0) {
        warn("could not open dev directory");
        return 1;
    }
    auto ctld_fd = open("/sys/class/zram-control", O_DIRECTORY | O_PATH);
    if (ctld_fd < 0) {
        warn("could not open zram control directory");
        return 1;
    }
    struct stat st;
    if (fstatat(dev_fd, zdev, &st, 0)) {
        /* try requesting devices until we get one */
        for (;;) {
            auto ha_fd = openat(ctld_fd, "hot_add", O_RDONLY);
            if (ha_fd < 0) {
                warn("could not open zram hot_add file");
                close(dev_fd);
                close(ctld_fd);
                return 1;
            }
            char buf[32], *errp = nullptr;
            long devn;
            auto devnr = read(ha_fd, buf, sizeof(buf));
            if (devnr <= 0) {
                warn("could not request new zram device");
                goto err_case;
            }
            devn = std::strtol(buf, &errp, 10);
            if (!errp || (*errp && !std::isspace(*errp))) {
                warnx("invalid output from zram hot_add");
                goto err_case;
            }
            if (devn < 0) {
                errno = devn;
                warn("could not request zram device");
                goto err_case;
            }
            if (devn > znum) {
                warnx("could not request zram device");
                goto err_case;
            } else if (devn == znum) {
                /* got the one */
                break;
            } else {
                /* need to request more */
                continue;
            }
err_case:
            close(dev_fd);
            close(ctld_fd);
            close(ha_fd);
            return 1;
        }
        if (fstatat(dev_fd, zdev, &st, 0)) {
            warn("could not request zram device '%s'", zdev);
            close(dev_fd);
            close(ctld_fd);
            return 1;
        }
    }
    if (!S_ISBLK(st.st_mode)) {
        warnx("'%s' is not a block device", zdev);
        close(dev_fd);
        close(ctld_fd);
        return 1;
    }
    close(dev_fd);
    close(ctld_fd);
    /* now get /sys/block... */
    auto bfd = open("/sys/block", O_DIRECTORY | O_PATH);
    if (bfd < 0) {
        warn("could not open '/sys/block'");
        return 1;
    }
    /* and the zram device we need */
    auto zfd = openat(bfd, zdev, O_DIRECTORY | O_PATH);
    if (zfd < 0) {
        warn("could not open '/sys/block/%s'", zdev);
        close(bfd);
        return 1;
    }
    close(bfd);
    /* and we can go wild, first reset though */
    if (!write_param(zfd, zdev, "reset", "1")) {
        close(zfd);
        return 1;
    }
    /* set the algorithm if we have it, need that first */
    if (zram_algo.size()) {
        if (!write_param(zfd, zdev, "comp_algorithm", zram_algo.data())) {
            close(zfd);
            return 1;
        }
        if (zram_algo_params.size() && !write_param(
            zfd, zdev, "algorithm_params", zram_algo_params.data()
        )) {
            close(zfd);
            return 1;
        }
    }
    /* set the writeback device if expected */
    if (zram_backing_dev.size()) {
        if (!write_param(
            zfd, zdev, "backing_dev", zram_backing_dev.data()
        )) {
            close(zfd);
            return 1;
        }
        if (zram_writeback_limit.size()) {
            if (!write_param(zfd, zdev, "writeback_limit_enable", "1")) {
                close(zfd);
                return 1;
            }
            if (!write_param(
                zfd, zdev, "writeback_limit", zram_writeback_limit.data()
            )) {
                close(zfd);
                return 1;
            }
        }
    }
    /* set the size */
    if (!write_param(zfd, zdev, "disksize", zram_size.data())) {
        close(zfd);
        return 1;
    }
    /* set the mem limit */
    if (zram_mem_limit.size() && !write_param(
        zfd, zdev, "mem_limit", zram_mem_limit.data()
    )) {
        close(zfd);
        return 1;
    }
    std::printf("set up device, formatting...\n");
    close(zfd);
    return zram_format(zdev);
}

static int stop_zram(char const *zdev) {
    auto bfd = open("/sys/block", O_DIRECTORY | O_PATH);
    if (bfd < 0) {
        warn("could not open '/sys/block'");
        return 1;
    }
    auto zfd = openat(bfd, zdev, O_DIRECTORY | O_PATH);
    if (zfd < 0) {
        warn("could not open '/sys/block/%s'", zdev);
        close(bfd);
        return 1;
    }
    close(bfd);
    auto hrfd = open("/sys/class/zram-control/hot_remove", O_WRONLY);
    if (hrfd < 0) {
        warn("could not open zram hot_remove");
        return 1;
    }
    if (write_param(zfd, zdev, "reset", "1")) {
        write_param(hrfd, zdev, nullptr, zdev + 4);
    }
    close(zfd);
    close(hrfd);
    return 0;
}

static bool load_conf(
    char const *s, char *&line, std::size_t &len, char const *zsect
) {
    FILE *f = std::fopen(s, "rb");
    if (!f) {
        warnx("could not load '%s'", s);
        return false;
    }
    bool fret = true;
    bool in_sect = false;
    auto slen = std::strlen(zsect);
    for (ssize_t nread; (nread = getline(&line, &len, f)) != -1;) {
        /* strip leading whitespace and ignore comments, empty lines etc */
        char *cline = line;
        while (std::isspace(*cline)) {
            ++cline;
        }
        if ((*cline == '#') || (*cline == ';') || !*cline) {
            continue;
        }
        /* strip leading spaces */
        while (std::isspace(*cline)) {
            ++cline;
        }
        /* strip trailing spaces */
        auto rl = std::strlen(line);
        while (std::isspace(line[rl - 1])) {
            line[--rl] = '\0';
        }
        if (*cline == '[') {
            in_sect = !std::strncmp(cline + 1, zsect, slen);
            if ((cline[slen + 1] != ']') || cline[slen + 2]) {
                warnx("invalid syntax: '%s'", cline);
                return false;
            }
            continue;
        }
        /* skip sections not relevant to us */
        if (!in_sect) {
            continue;
        }
        auto *eq = std::strchr(cline, '=');
        if (!eq) {
            warnx("invalid syntax: '%s'", cline);
            return false;
        }
        *eq = '\0';
        auto *key = cline;
        auto *value = eq + 1;
        /* strip spaces before assignment */
        while ((eq != cline) && std::isspace(*(eq - 1))) {
            *--eq = '\0';
        }
        /* strip spaces after assignment */
        while (std::isspace(*value)) {
            ++value;
        }
        if (!*value) {
            warnx("empty value for key '%s'", key);
            return false;
        }
        if (!std::strcmp(key, "size")) {
            zram_size = value;
        } else if (!std::strcmp(key, "algorithm")) {
            zram_algo = value;
            /* parse the parameters */
            char *algop = zram_algo.data();
            auto *paren = std::strchr(algop, '(');
            if (paren) {
                char *endp = std::strchr(paren + 1, ')');
                if (!endp || endp[1]) {
                    warnx("malformed algorithm value '%s'", zram_algo.data());
                    return false;
                }
                char *pbeg = paren + 1;
                while ((paren != algop) && std::isspace(*(paren - 1))) {
                    --paren;
                }
                *paren = '\0';
                /* just in case the contents of parens are all spaces */
                while ((pbeg != endp) && std::isspace(*pbeg)) {
                    ++pbeg;
                }
                /* terminate at ) */
                *endp = '\0';
                /* now algop is just algorithm name, write it into params */
                if (pbeg != endp) {
                    zram_algo_params += "algo=";
                    zram_algo_params += algop;
                    for (;;) {
                        /* strip leading spaces */
                        while (std::isspace(*pbeg)) {
                            ++pbeg;
                        }
                        auto *cpend = std::strchr(pbeg, ',');
                        char *comma = nullptr;
                        if (cpend) {
                            comma = cpend + 1;
                            *cpend = '\0';
                        } else {
                            cpend = endp;
                        }
                        /* strip trailing spaces */
                        while ((cpend != pbeg) && std::isspace(*(cpend - 1))) {
                            --cpend;
                        }
                        *cpend = '\0';
                        if (pbeg == cpend) {
                            warnx("algorithm parameter must not be empty");
                            return false;
                        }
                        zram_algo_params.push_back(' ');
                        zram_algo_params += pbeg;
                        if (!comma) {
                            break;
                        }
                        pbeg = comma;
                    }
                }
                /* finally shrink the algorithm name just in case */
                zram_algo.resize(paren - algop);
            }
        } else if (!std::strcmp(key, "format")) {
            zram_fmt = value;
        } else if (!std::strcmp(key, "mem_limit")) {
            zram_mem_limit = value;
        } else if (!std::strcmp(key, "writeback_limit")) {
            zram_writeback_limit = value;
        } else if (!std::strcmp(key, "backing_dev")) {
            zram_backing_dev = value;
        } else {
            warnx("unknown key '%s'", key);
            return false;
        }
    }
    std::fclose(f);
    return fret;
}

int main(int argc, char **argv) {
    if (geteuid() != 0) {
        errx(1, "this program must be run as root");
    }

    if ((argc != 2) && (argc != 3)) {
        warnx("incorrect number of arguments");
        usage(stderr);
        return 1;
    }

    char const *zramname = argv[1];
    if (std::strncmp(zramname, "zram", 4)) {
        warnx("incorrect device specified");
        usage(stderr);
        return 1;
    }
    char *errp = nullptr;
    auto znum = std::strtoul(zramname + 4, &errp, 10);
    if (!errp || *errp || (znum > 99)) {
        warnx("incorrect device specified");
        usage(stderr);
        return 1;
    }

    struct stat st;
    /* ensure we've got zram loaded */
    if (stat("/sys/class/zram-control", &st)) {
        errx(1, "zram is not loaded");
    }

    char *line = nullptr;
    std::size_t len = 0;

    if (argc == 3) {
        if (!std::strcmp(argv[2], "stop")) {
            return stop_zram(zramname);
        }
        if (access(argv[2], R_OK)) {
            err(1, "could not access '%s'", argv[2]);
        }
        if (!load_conf(argv[2], line, len, zramname)) {
            return 1;
        }
        std::free(line);
        return setup_zram(zramname, znum);
    }

    std::unordered_map<std::string, std::string> got_map;

    for (char const **p = paths; *p; ++p) {
        int dfd = open(*p, O_RDONLY | O_DIRECTORY);
        if (dfd < 0) {
            continue;
        }
        int dupfd = dup(dfd);
        if (dupfd < 0) {
            err(1, "dupfd");
        }
        DIR *dirp = fdopendir(dupfd);
        if (!dirp) {
            err(1, "fdopendir");
        }
        struct dirent *dp;
        while ((dp = readdir(dirp))) {
            /* must be a regular file or a symlink to regular file; we cannot
             * use d_type (nonportable anyway) because that will get DT_LNK
             * for symlinks (it does not follow)
             */
            struct stat st;
            if ((fstatat(dfd, dp->d_name, &st, 0) < 0) || !S_ISREG(st.st_mode)) {
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
        close(dfd);
        closedir(dirp);
    }

    std::vector<std::string const *> ord_list;

    /* construct a sorted vector of names, backed by map memory */
    for (auto &p: got_map) {
        ord_list.push_back(&p.first);
    }

    std::sort(ord_list.begin(), ord_list.end(), [](auto a, auto b) {
        return (*a < *b);
    });

    for (auto &c: ord_list) {
        if (!load_conf(got_map[*c].data(), line, len, zramname)) {
            return 1;
        }
    }
    /* global dinit-zram.conf is last if it exists */
    if (!access(sys_path, R_OK)) {
        char const *asysp = strchr(sys_path, '/') + 1;
        /* only load if no file called dinit-zram.conf was already handled */
        if (got_map.find(asysp) == got_map.end()) {
            if (!load_conf(sys_path, line, len, zramname)) {
                return 1;
            }
        }
    }
    std::free(line);

    return setup_zram(zramname, znum);
}
