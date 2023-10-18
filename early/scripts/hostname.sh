#!/bin/sh

DINIT_SERVICE=hostname
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

[ -r /etc/hostname ] && read -r HOSTNAME < /etc/hostname
[ -z "$HOSTNAME"   ] && HOSTNAME=chimera

printf "%s" "$HOSTNAME" > /proc/sys/kernel/hostname
