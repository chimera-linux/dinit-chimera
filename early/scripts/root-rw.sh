#!/bin/sh

DINIT_SERVICE=root-rw
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/mnt root-rw
