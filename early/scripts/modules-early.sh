#!/bin/sh

DINIT_SERVICE=modules-early
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

for f in $(kmod static-nodes 2> /dev/null | awk '/Module/ {print $2}'); do
    modprobe -bq "$f" 2> /dev/null
done
