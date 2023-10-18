#!/bin/sh

DINIT_SERVICE=tmpfs

. ./early/scripts/common.sh

umask 022
set -e

mountpoint -q /run || mount -o mode=0755,nosuid,nodev -t tmpfs run /run

# readable system state
mkdir -p /run/dinit

# now that we a /run, expose container as state file too (for shutdown etc)
if [ -n "$DINIT_CONTAINER" ]; then
    touch /run/dinit/container
fi

# ditto
if [ -n "$DINIT_FIRST_BOOT" ]; then
    touch /run/dinit/first-boot
fi
