#!/bin/sh

DINIT_SERVICE=${1:-console}
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v setupcon > /dev/null 2>&1 || exit 0

if [ "$1" = "keyboard" ]; then
    set -- "-k"
else
    set --
fi

exec setupcon "$@"
