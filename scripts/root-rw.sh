#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

[ -e /run/dinit/container ] && exit 0

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

mount -o remount,rw /
