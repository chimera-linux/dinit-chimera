#!/bin/sh

DINIT_SERVICE=dmraid
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v dmraid > /dev/null 2>&1 || exit 0

exec dmraid -i -ay
