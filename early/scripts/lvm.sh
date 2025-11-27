#!/bin/sh

DINIT_SERVICE=lvm
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

command -v vgchange > /dev/null 2>&1 || exit 0

if [ -r /proc/cmdline ]; then
    for x in $(cat /proc/cmdline); do
        case "$x" in
            dinit_skip_volumes) exit 0 ;;
        esac
    done
fi

case "$1" in
    start) exec vgchange --sysinit -a ay ;;
    stop)
        if [ $(vgs | wc -l) -gt 0 ]; then
            exec vgchange -an
        fi
        ;;
esac
