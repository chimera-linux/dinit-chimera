#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

[ -r /etc/hwclock ] && read -r HWCLOCK < /etc/hwclock

case "$HWCLOCK" in
    utc|localtime)
        echo "Setting RTC to '${HWCLOCK}'..."
        hwclock --systz ${HWCLOCK:+--${HWCLOCK} --noadjfile} || exit 1
        ;;
esac
