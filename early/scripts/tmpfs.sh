#!/bin/sh

DINIT_SERVICE=tmpfs

. ./early/scripts/common.sh

umask 022
set -e

mountpoint -q /run || mount -o mode=0755,nosuid,nodev -t tmpfs run /run

# readable system state
mkdir -p /run/dinit

# detect if running in a container, expose it globally
if [ -n "${container+x}" ]; then
    touch /run/dinit/container
    dinitctl setenv DINIT_CONTAINER=1
fi

# detect first boot
if [ ! -e /etc/machine-id ]; then
    touch /run/dinit/first-boot
    dinitctl setenv DINIT_FIRST_BOOT=1
elif [ "$(cat /etc/machine-id)" = "uninitialized" ]; then
    touch /run/dinit/first-boot
    dinitctl setenv DINIT_FIRST_BOOT=1
fi
