#ifndef CLOCK_COMMON_H
#define CLOCK_COMMON_H

#include <cstdio>
#include <cstring>

typedef enum {
    RTC_MOD_UTC,
    RTC_MOD_LOCALTIME,
} rtc_mod_t;

static rtc_mod_t rtc_mod_guess(void) {
    rtc_mod_t ret = RTC_MOD_UTC;

    FILE *f = fopen("/etc/adjtime", "r");
    if (!f) {
        return RTC_MOD_UTC;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        /* last line will decide it, compliant file should be 3 lines */
        if (!strncmp(buf, "LOCAL", 5)) {
            ret = RTC_MOD_LOCALTIME;
            break;
        } else if (!strncmp(buf, "UTC", 3)) {
            ret = RTC_MOD_UTC;
            break;
        }
    }

    fclose(f);
    return ret;
}

#endif
