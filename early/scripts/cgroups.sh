#!/bin/sh

DINIT_SERVICE=cgroups
DINIT_NO_CONTAINER=1

set -e

. ./early/scripts/common.sh

mkdir -p "/sys/fs/cgroup"
./early/helpers/mntpt "/sys/fs/cgroup" || mount -t cgroup2 -o nsdelegate cgroup2 "/sys/fs/cgroup"
