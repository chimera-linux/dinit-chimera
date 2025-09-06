#!/bin/sh

DINIT_SERVICE=devmon

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/devmon "$1"
