#!/bin/sh

DINIT_SERVICE="cryptdisks-${1:-unknown}"
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

[ -x @DINIT_CRYPTDISKS_PATH@ ] || exit 0

exec @DINIT_CRYPTDISKS_PATH@ "$@"
