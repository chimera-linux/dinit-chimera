#!/bin/sh

DINIT_SERVICE=lvm
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v vgchange > /dev/null 2>&1 || exit 0

case "$1" in
    start) exec vgchange --sysinit -a ay ;;
    stop)
        if [ $(vgs | wc -l) -gt 0 ]; then
            exec vgchange -an
        fi
        ;;
esac
