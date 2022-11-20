#!/bin/sh

[ -z "${container+x}"  ] || exit 0
[ -x /usr/bin/vgchange ] || exit 0

case "$1" in
    start) /usr/bin/vgchange --sysinit -a ay ;;
    stop)
        if [ $(vgs | wc -l) -gt 0 ]; then
            /usr/bin/vgchange -an
        fi
        ;;
esac
