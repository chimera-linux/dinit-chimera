#!/bin/sh

for f in $(/usr/bin/kmod static-nodes 2> /dev/null | /usr/bin/awk '/Module/ {print $2}'); do
    modprobe -bq "$f" 2> /dev/null
done
