#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

[ -e /run/dinit/container ] && exit 0
command -v vgchange > /dev/null 2>&1 || exit 0

case "$1" in
    start) vgchange --sysinit -a ay ;;
    stop)
        if [ $(vgs | wc -l) -gt 0 ]; then
            vgchange -an
        fi
        ;;
esac
