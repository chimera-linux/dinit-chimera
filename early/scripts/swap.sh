#!/bin/sh

DINIT_SERVICE=swap
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/swap "$1"
