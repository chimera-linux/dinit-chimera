#!/bin/sh

DINIT_SERVICE=mdadm
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v mdadm > /dev/null 2>&1 || exit 0

exec mdadm -As
