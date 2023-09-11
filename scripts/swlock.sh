#!/bin/sh

# container environment
[ -e /run/dinit/container ] && exit 0

case "$1" in
    start|stop) ;;
    *) exit 1 ;;
esac

/usr/libexec/dinit/helpers/swclock "$1" || :
