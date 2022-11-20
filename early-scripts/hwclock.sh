#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

[ -r /etc/hwclock ] && read -r HWCLOCK < /etc/hwclock

case "$HWCLOCK" in
    utc|localtime)
        case "$1" in
            start)
                hwclock --systz ${HWCLOCK:+--${HWCLOCK} --noadjfile}
                ;;
            stop)
                hwclock --systohc ${HWCLOCK:+--${HWCLOCK}}
                ;;
            *) exit 1 ;;
        esac
        ;;
esac
