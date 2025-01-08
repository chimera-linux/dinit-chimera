#!/bin/sh

DINIT_SERVICE=devmon
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

[ -x @HELPER_PATH@/devmon ] || exit 0
exec @HELPER_PATH@/devmon
