#!/bin/sh

set -e

mountpoint -q /run || mount -o mode=0755,nosuid,nodev -t tmpfs run /run

if [ -n "${container+x}" ]; then
    touch /run/system_is_container
fi
