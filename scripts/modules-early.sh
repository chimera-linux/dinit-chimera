#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

[ -e /run/dinit/container ] && exit 0

for f in $(kmod static-nodes 2> /dev/null | awk '/Module/ {print $2}'); do
    modprobe -bq "$f" 2> /dev/null
done
