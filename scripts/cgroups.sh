#!/bin/sh

set -e

[ -z "${container+x}" ] || exit 0

mkdir -p "/sys/fs/cgroup"
mountpoint -q "/sys/fs/cgroup" || mount -t cgroup2 -o nsdelegate cgroup2 "/sys/fs/cgroup"
