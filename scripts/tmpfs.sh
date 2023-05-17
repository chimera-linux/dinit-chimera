#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

umask 022
set -e

mountpoint -q /run || mount -o mode=0755,nosuid,nodev -t tmpfs run /run

# readable system state
mkdir -p /run/dinit

# detect if running in a container, expose it globally
if [ -n "${container+x}" ]; then
    touch /run/dinit/container
fi

# detect first boot
if [ ! -e /etc/machine-id ]; then
    touch /run/dinit/first-boot
elif [ "$(cat /etc/machine-id)" = "uninitialized" ]; then
    touch /run/dinit/first-boot
fi
