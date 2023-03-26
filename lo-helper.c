#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <net/if.h>
#include <sys/ioctl.h>

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
