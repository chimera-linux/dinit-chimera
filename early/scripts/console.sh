#!/bin/sh

DINIT_SERVICE=${1:-console}
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

[ -x @DINIT_CONSOLE_PATH@ ] || exit 0

exec @DINIT_CONSOLE_PATH@ "$1"
