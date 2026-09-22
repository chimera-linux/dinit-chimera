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
#include <functional>

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

std::unordered_map<std::string, double> zram_bvars;
std::unordered_map<std::string, double> zram_vars;

static unsigned long long zram_size = 0;
static unsigned long long zram_mem_limit = 0;

static std::string zram_algo{};
static std::string zram_algo_params{};
static std::string zram_backing_dev{};
static std::string zram_writeback_limit{};
static std::string zram_fmt = "mkswap -U clear %0";

/* some minor string utilities */

template<typename T>
static void strip_lead(T &&str, std::size_t *len = nullptr) {
    while ((!len || *len) && std::isspace(*str)) {
        ++str;
        if (len) {
          --*len;
        }
    }
}

template<typename T>
static void strip_lead(T &&str, char const *endp) {
    while ((str != endp) && std::isspace(*str)) {
        ++str;
    }
}

static void strip_trail(char const *str, std::size_t &len) {
    while (len && std::isspace(str[len - 1])) {
        --len;
    }
}

static std::size_t strip_trail(char *str) {
    auto rl = std::strlen(str);
    while (rl && std::isspace(str[rl - 1])) {
        str[--rl] = '\0';
    }
    return rl;
}

/* helper stack for putting function arguments on */
std::vector<double> sexp_argstack;

struct f_nop {
    double operator()(double arg) { return arg; }
};

#define F_BINOP(name, func) struct f_##name { \
    double operator()(double a, double b) { return func(a, b); } \
};

F_BINOP(mod, std::fmod)
F_BINOP(pow, std::pow)

#define F_UNOP(name) {#name, [](double *args, std::size_t argn) { \
    if (!argn) { \
        return std::name(0.0); \
    } \
    return std::name(*args); \
}}

template<typename BOP, typename UOP>
struct f_arith {
    double operator()(double *args, std::size_t argn) {
        switch (argn) {
            case 0: return 0;
            case 1: return UOP{}(*args);
            default: break;
        }
        double ret = *args++;
        auto op = BOP{};
        while (--argn) {
            ret = op(ret, *args++);
        }
        return ret;
    }
};

template<typename OP>
struct f_comp {
    double operator()(double *args, std::size_t argn) {
        if (!argn--) {
            return 0;
        }
        auto op = OP{};
        if (argn == 1) {
            return double(op(*args, 0));
        }
        bool val = op(args[0], args[1]);
        for (std::size_t i = 2; i < argn && val; ++i) {
            val = op(args[i - 1], args[i]);
        }
        return double(val);
    }
};

std::unordered_map<
    std::string, std::function<double(double *, std::size_t)>
> sexp_funcs = {
    {"+", f_arith<std::plus<double>, f_nop>{}},
    {"-", f_arith<std::minus<double>, std::negate<double>>{}},
    {"*", f_arith<std::multiplies<double>, f_nop>{}},
    {"/", f_arith<std::divides<double>, f_nop>{}},
    {"%", f_arith<f_mod, f_nop>{}},
    {"^", f_arith<f_pow, f_nop>{}},
    {"==", f_comp<std::equal_to<double>>{}},
    {"!=", f_comp<std::not_equal_to<double>>{}},
    {">", f_comp<std::greater<double>>{}},
    {">=", f_comp<std::greater_equal<double>>{}},
    {"<", f_comp<std::less<double>>{}},
    {"<=", f_comp<std::less_equal<double>>{}},
    {"&&", [](double *args, std::size_t argn) {
        if (!argn) {
            return 1.0;
        }
        double ret;
        for (std::size_t i = 0; i < argn; ++i) {
            if (!args[i]) {
                return args[i];
            }
            ret = args[i];
        }
        return ret;
    }},
    {"||", [](double *args, std::size_t argn) {
        if (!argn) {
            return 0.0;
        }
        double ret;
        for (std::size_t i = 0; i < argn; ++i) {
            if (args[i]) {
                return args[i];
            }
            ret = args[i];
        }
        return ret;
    }},
    {"?", [](double *args, std::size_t argn) {
        if (!argn) {
            return 0.0;
        }
        if (args[0]) {
            return ((argn > 1) ? args[1] : 0.0);
        }
        return ((argn > 2) ? args[2] : 0.0);
    }},
    {"min", [](double *args, std::size_t argn) {
        if (!argn) {
            return 0.0;
        }
        double ret = args[0];
        for (std::size_t i = 1; i < argn; ++i) {
            ret = std::min(ret, args[i]);
        }
        return ret;
    }},
    {"max", [](double *args, std::size_t argn) {
        if (!argn) {
            return 0.0;
        }
        double ret = args[0];
        for (std::size_t i = 1; i < argn; ++i) {
            ret = std::max(ret, args[i]);
        }
        return ret;
    }},
    {"sign", [](double *args, std::size_t argn) {
        if (!argn) {
            return 0.0;
        }
        return static_cast<double>((0.0 < *args) - (*args < 0.0));
    }},
    {"int", [](double *args, std::size_t argn) {
        if (!argn) {
            return 0.0;
        }
        return static_cast<double>(static_cast<long long>(*args));
    }},
    {"log", [](double *args, std::size_t argn) {
        switch (argn) {
            case 0:
                return std::log(0.0);
            case 1:
                return std::log(*args);
            default:
                break;
        }
        return std::log(args[0]) / std::log(args[1]);
    }},
    F_UNOP(floor),
    F_UNOP(ceil),
    F_UNOP(round),
    F_UNOP(abs),
    F_UNOP(sin),
    F_UNOP(cos),
    F_UNOP(tan),
    F_UNOP(asin),
    F_UNOP(acos),
    F_UNOP(atan),
    F_UNOP(sinh),
    F_UNOP(cosh),
    F_UNOP(tanh),
    F_UNOP(asinh),
    F_UNOP(acosh),
    F_UNOP(atanh),
};

/* Any suffficiently complicated C(++) program contains an ad-hoc,
 * informally specified, bug-ridden, slow implementation of Lisp
 *
 * mainly i didn't feel like implementing a lexer or precedence climbing
 *
 * maybe stricter error handling later, for now it's very loose so it cannot
 * reasonably fail, which may be okay enough for this kind of purpose
 *
 * nothing here terminates strings, so we manage the length carefully
 */
static double eval_exp(char const *&value, std::size_t &vlen) {
    if (vlen == 0) {
        warnx("got empty expression");
        return 0;
    }
    /* start by checking what kind of expression we have
     *
     * it can be either a s-expression call, or a literal, or a variable */
    if (*value == '(') {
        /* strip the leading paren and spaces... */
        ++value;
        --vlen;
        strip_lead(value, &vlen);
        if (!vlen) {
            warnx("found empty s-expression");
            return 0;
        }
        /* locate the function only */
        std::size_t wlen = 0;
        char const *wbeg = value;
        while (vlen && !std::isspace(*value) && (*value != ')')) {
            ++wlen;
            ++value;
            --vlen;
        }
        std::string funcn{wbeg, wlen};
        /* see if the function exists... */
        bool nop = false;
        auto it = sexp_funcs.find(funcn);
        if (it == sexp_funcs.end()) {
            warnx("unknown function '%s'", funcn.data());
            /* we still need to parse the rest so don't fail now */
            nop = true;
        }
        /* need at least one argument for valid code */
        strip_lead(value, &vlen);
        if (!vlen || (*value == ')')) {
            warnx("function '%s' called without arguments", funcn.data());
            if (vlen) {
                ++value;
                --vlen;
                strip_lead(value, &vlen);
            }
            if (nop) {
                return 0;
            }
            return it->second(nullptr, 0);
        }
        std::size_t oldn = sexp_argstack.size();
        std::size_t argn = 0;
        while (vlen && (*value != ')')) {
            sexp_argstack.push_back(eval_exp(value, vlen));
            ++argn;
        }
        /* call the thing */
        double ret = 0;
        if (!nop) {
            ret = it->second(&sexp_argstack.data()[oldn], argn);
        }
        /* drop the args */
        while (argn--) {
            sexp_argstack.pop_back();
        }
        strip_lead(value, &vlen);
        if (!vlen || (*value != ')')) {
            /* just warn lol */
            warnx("no matching ')' for '%s'", value);
        } else {
            /* strip the trailing ) */
            ++value;
            --vlen;
        }
        strip_lead(value, &vlen);
        return ret;
    }
    /* variable access */
    if (std::isalpha(*value) || (*value == '_')) {
        /* fetch the word */
        std::size_t wlen = 0;
        for (std::size_t i = 0; i < vlen; ++i) {
            if (!std::isalnum(value[i]) && (value[i] != '_')) {
                if (std::isspace(value[i]) || (value[i] == ')')) {
                    /* we reached somewhere outside */
                    break;
                }
                warnx(
                    "invalid character '%c' in variable '%.*s'",
                    value[i], int(wlen), value
                );
            }
            ++wlen;
        }
        std::string varn{value, wlen};
        /* advance the stream */
        value += wlen;
        vlen -= wlen;
        strip_lead(value, &vlen);
        /* look up the var */
        auto it = zram_vars.find(varn);
        if (it == zram_vars.end()) {
            warnx("undefined variable '%s'", varn.data());
            return 0;
        }
        return it->second;
    }
    /* literal value */
    char *endp = nullptr;
    char const *valbeg = value;
    double v = std::strtod(value, &endp);
    /* parse failure? */
    if (!endp || (endp == value)) {
        warnx("invalid literal value '%c'", *value);
        return v;
    }
    /* advance the stream */
    vlen -= std::size_t(endp - value);
    value = endp;
    /* we parsed a number, see if there is a suffix */
    if (vlen) {
        unsigned long long mul = 1;
        switch (*endp) {
            case 'T':
                mul *= 1024;
            case 'G':
                mul *= 1024;
            case 'M':
                mul *= 1024;
            case 'K':
                v *= (1024 * mul);
                vlen -= 1;
                value += 1;
            default:
                break;
        }
    }
    /* garbage */
    bool bad = false;
    while (vlen && !std::isspace(*value) && (*value != ')')) {
        ++value;
        --vlen;
        bad = true;
    }
    if (bad) {
        warnx("invalid suffix in literal '%.*s'", int(value - valbeg), valbeg);
    }
    strip_lead(value, &vlen);
    return v;
}

/* convenience, not used in the parser */
static double eval_exp_var(char const *value, std::size_t vlen) {
    strip_lead(value, &vlen);
    auto ret = eval_exp(value, vlen);
    if (vlen) {
        warnx("trailing garbage after expression: '%s'", value);
    }
    return ret;
}

static unsigned long long eval_exp_int(char const *value) {
    return static_cast<unsigned long long>(
        eval_exp_var(value, std::strlen(value))
    );
}

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
    strip_lead(data);
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
    if (!zram_size) {
        warnx("no size specified for '%s'", zdev);
        return 1;
    }
    std::printf(
        "setting up device '%s' with size %llu...\n", zdev, zram_size
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
    char zsize[64];
    std::snprintf(zsize, sizeof(zsize), "%llu", zram_size);
    if (!write_param(zfd, zdev, "disksize", zsize)) {
        close(zfd);
        return 1;
    }
    /* set the mem limit */
    std::snprintf(zsize, sizeof(zsize), "%llu", zram_mem_limit);
    if (zram_mem_limit && !write_param(zfd, zdev, "mem_limit", zsize)) {
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
    bool in_cursect = false;
    bool in_sect = false;
    for (ssize_t nread; (nread = getline(&line, &len, f)) != -1;) {
        /* strip leading whitespace and ignore comments, empty lines etc */
        char *cline = line;
        strip_lead(cline);
        if ((*cline == '#') || (*cline == ';') || !*cline) {
            continue;
        }
        auto rl = strip_trail(line);
        if (*cline == '[') {
            /* make sure it's terminated */
            if ((cline[rl - 1] != ']')) {
                warnx("invalid syntax: '%s'", cline);
                return false;
            }
            in_sect = true; /* we are in *some* section */
            /* the entire string inside has to match, so terminate at bracket */
            cline[rl - 1] = '\0';
            in_cursect = !std::strcmp(cline + 1, zsect);
            continue;
        }
        /* outside of sections, we only need to handle directives */
        if (!in_sect) {
            if (strncmp(cline, "set!", 4)) {
                warnx("invalid syntax: '%s'", cline);
                return false;
            }
            /* find the delimiter while the full line is intact */
            auto *eq = std::strchr(cline, '=');
            if (!eq) {
                warnx("invalid syntax: '%s'", cline);
                return false;
            }
            /* terminate at delimiter so we have just the name/value later */
            *eq = '\0';
            /* advance the set! */
            cline += 4;
            strip_lead(cline);
            auto *varv = eq + 1;
            strip_lead(varv);
            auto vlen = strip_trail(varv);
            /* variable names have to be [a-zA-Z_] */
            if (!std::isalpha(*cline) && (*cline != '_')) {
                warnx("invalid variable name: '%s'", cline);
                return false;
            }
            /* validate the rest of the name, can be [a-zA-Z0-9_] */
            for (auto *cl = cline + 1; *cl; ++cl) {
                if (!std::isalnum(*cl) && (*cl != '_')) {
                    warnx("invalid variable name: '%s'", cline);
                    return false;
                }
            }
            /* can't override builtins */
            auto it = zram_bvars.find(cline);
            if (it != zram_bvars.end()) {
                warnx("attempt to override builtin variable '%s'", cline);
                return false;
            }
            /* make sure value is non-empty */
            if (!*varv) {
                warnx("invalid value for variable '%s' (empty)", cline);
                return false;
            }
            /* direct access without shell is pretty straightforward */
            if (*varv != '`') {
                zram_vars[cline] = eval_exp_var(varv, vlen);
                continue;
            }
            /* here we make sure we trail with a '`' too and strip it */
            ++varv;
            strip_lead(varv);
            vlen = std::strlen(varv);
            if (!vlen || varv[vlen - 1] != '`') {
                warnx(
                    "invalid value for evaluated variable '%s' (missing trailing backtick)",
                    cline
                );
                return false;
            }
            varv[--vlen] = '\0';
            strip_trail(varv, vlen);
            if (!vlen) {
                warnx("invalid value for evaluated variable '%s' (empty command)", cline);
                return false;
            }
            /* re-terminate the inner command */
            varv[vlen] = '\0';
            /* 100% safety guaranteed
             *
             * a single arbitrary length line is read
             *
             * only use with commands that won't block it :)
             *
             * maybe later we'll do more safe thing with pipes and timeout
             */
            auto *fp = popen(varv, "r");
            if (!fp) {
                warn("popen failed for %s=%s, using 0", cline, varv);
                zram_vars[cline] = 0;
                continue;
            }
            char *lptr = nullptr;
            size_t lsize = 0;
            auto llen = getline(&lptr, &lsize, fp);
            if (llen < 0) {
                warn("line read failed for %s=%s, using 0", cline, varv);
                zram_vars[cline] = 0;
                pclose(fp);
                std::free(lptr);
                continue;
            }
            pclose(fp);
            /* get rid of any leading whitespace */
            auto *valv = lptr;
            strip_lead(valv);
            strip_trail(valv, vlen);
            if (!vlen) {
                warnx("empty line received for %s=%s, using 0", cline, varv);
                zram_vars[cline] = 0;
                std::free(lptr);
                continue;
            }
            /* re-terminate at length */
            valv[vlen] = '\0';
            /* eval as expression and store */
            zram_vars[cline] = eval_exp_var(valv, vlen);
            /* free and continue */
            std::free(lptr);
            continue;
        }
        /* if we're in a section but not ours, we don't care */
        if (!in_cursect) {
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
        strip_lead(value);
        if (!*value) {
            warnx("empty value for key '%s'", key);
            return false;
        }
        if (!std::strcmp(key, "size")) {
            zram_size = eval_exp_int(value);
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
                strip_lead(pbeg, endp);
                /* terminate at ) */
                *endp = '\0';
                /* now algop is just algorithm name, write it into params */
                if (pbeg != endp) {
                    zram_algo_params += "algo=";
                    zram_algo_params += algop;
                    for (;;) {
                        /* strip leading spaces */
                        strip_lead(pbeg);
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
            zram_mem_limit = eval_exp_int(value);
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

    /* use a minimal C locale to avoid strtod formatting issues */
    setlocale(LC_NUMERIC, "C");

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

    /* populate builtin vars */
    zram_bvars["pi"] = M_PI;
    zram_bvars["e"] = M_E;

    /* get total ram with a single read, linux ofc makes it a pain in the ass */
    char mbuf[1024] = {};
    int minfo = open("/proc/meminfo", O_RDONLY);
    if (minfo < 0) {
        err(1, "could not open /proc/meminfo");
    }
    auto n = read(minfo, mbuf, sizeof(mbuf) - 1);
    if (n < 0) {
        err(1, "could not read /proc/meminfo");
    }
    if (std::strncmp(mbuf, "MemTotal:", sizeof("MemTotal:") - 1)) {
        errx(1, "malformed /proc/meminfo read (no MemTotal)");
    }
    char *mt = mbuf + sizeof("MemTotal");
    strip_lead(mt);
    char *endp = nullptr;
    unsigned long long mtv = std::strtoull(mt, &endp, 10);
    if (!endp || !std::isspace(*endp)) {
        errx(1, "malformed /proc/meminfo read (invalid MemTotal format)");
    }
    strip_lead(endp);
    if (std::strncmp(endp, "kB\n", 3)) {
        errx(1, "malformed /proc/meminfo read (MemTotal not in kB)");
    }
    zram_bvars["ram"] = mtv * 1024;

    /* copy the builtin stuff */
    zram_vars = zram_bvars;

    /* reserve a bunch of slots in the arg stack */
    sexp_argstack.reserve(64);

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
