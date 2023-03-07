#!/bin/sh

set -e

mountpoint -q /run || mount -o mode=0755,nosuid,nodev -t tmpfs run /run
mkdir -p -m0755 /run/lvm /run/user /run/lock /run/log

if [ -n "${container+x}" ]; then
    touch /run/system_is_container
fi
