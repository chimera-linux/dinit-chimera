#!/bin/sh

DINIT_SERVICE=modules
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/kmod modules
