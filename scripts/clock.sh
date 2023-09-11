#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

# container environment
[ -e /run/dinit/container ] && exit 0

[ -r /etc/hwclock ] && read -r HWCLOCK < /etc/hwclock

case "$1" in
    hwclock|swclock) ;;
    *) exit 1 ;;
esac

HELPER=$1
shift

case "$1" in
    start|stop) ;;
    *) exit 1 ;;
esac

case "$HWCLOCK" in
    utc|localtime) set -- "$1" "$HWCLOCK" ;;
    *) set -- "$1" ;;
esac

/usr/libexec/dinit/helpers/$HELPER "$@" || :
