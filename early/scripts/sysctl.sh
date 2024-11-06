#!/bin/sh

DINIT_SERVICE=sysctl

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/sysctl
