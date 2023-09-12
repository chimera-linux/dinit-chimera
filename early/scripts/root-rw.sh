#!/bin/sh

DINIT_SERVICE=root-rw
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

# do not remount as rw if the intent is to stay as ro
if [ -r /etc/fstab ]; then
    ROOTFSOPTS=$(awk '{if ($2 == "/") print $4;}' /etc/fstab)
    IFS=, # loop the options which are comma-separated
    for opt in $ROOTFSOPTS; do
        if [ "$opt" = "ro" ]; then
            exit 0
        fi
    done
fi

exec mount -o remount,rw /
