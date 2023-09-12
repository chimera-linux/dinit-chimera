#!/bin/sh

DINIT_SERVICE=sysctl
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v sysctl > /dev/null 2>&1 || exit 0

exec sysctl --system
