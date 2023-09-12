#!/bin/sh

DINIT_SERVICE="${1:-clock}"
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

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

exec "./early/helpers/${HELPER}" "$@"
