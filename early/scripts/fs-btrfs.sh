#!/bin/sh

DINIT_SERVICE=fs-btrfs
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v btrfs > /dev/null 2>&1 || exit 0

exec btrfs device scan
