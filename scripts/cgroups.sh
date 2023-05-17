#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

set -e

[ -e /run/dinit/container ] && exit 0

mkdir -p "/sys/fs/cgroup"
mountpoint -q "/sys/fs/cgroup" || mount -t cgroup2 -o nsdelegate cgroup2 "/sys/fs/cgroup"
