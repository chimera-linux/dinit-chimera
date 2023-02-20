#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

[ -r /etc/hwclock ] && read -r HWCLOCK < /etc/hwclock

case "$1" in
    start|stop) ;;
    *) exit 1 ;;
esac

case "$HWCLOCK" in
    utc|localtime) set -- "$1" "$HWCLOCK" ;;
    *) set -- "$1" ;;
esac

/usr/libexec/hwclock-helper "$@" || :
