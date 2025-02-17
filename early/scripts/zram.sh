#!/bin/sh

DINIT_SERVICE=zram

. @SCRIPT_PATH@/common.sh

if [ -n "$DINIT_CONTAINER" ]; then
    echo "zram must not be used in containers"
    exit 1
fi

if [ "$1" = "stop" ]; then
   exec @HELPER_PATH@/zram "$2" stop
fi

# we need this loaded
@HELPER_PATH@/kmod load zram

exec @HELPER_PATH@/zram "$2"
