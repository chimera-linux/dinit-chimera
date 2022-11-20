#!/bin/sh

[ -r /etc/hostname ] && read -r HOSTNAME < /etc/hostname
[ -z "$HOSTNAME"   ] && HOSTNAME=chimera

printf "%s" "$HOSTNAME" > /proc/sys/kernel/hostname
