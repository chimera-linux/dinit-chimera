#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

[ -r /etc/hwclock ] && read -r HWCLOCK < /etc/hwclock

case "$HWCLOCK" in
    utc|localtime) hwclock --systohc ${HWCLOCK:+--${HWCLOCK}} ;;
esac
