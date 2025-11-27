#!/bin/sh

DINIT_SERVICE=fs-btrfs
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

command -v btrfs > /dev/null 2>&1 || exit 0

if [ -r /proc/cmdline ]; then
    for x in $(cat /proc/cmdline); do
        case "$x" in
            dinit_skip_volumes) exit 0 ;;
        esac
    done
fi

exec btrfs device scan
