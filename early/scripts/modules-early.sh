#!/bin/sh

DINIT_SERVICE=modules-early
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

MODFILE=/lib/modules/$(uname -r)/modules.devname
[ -r "$MODFILE" ] || exit 0

for f in $(awk '/^[^#]/ {print $1}' "$MODFILE"); do
    modprobe -bq "$f" 2> /dev/null
done
