#!/bin/sh

DINIT_SERVICE=root-remount
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec mount -o remount,${dinit_early_root_remount:-ro,rshared} /
