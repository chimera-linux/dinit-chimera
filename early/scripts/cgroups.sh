#!/bin/sh

DINIT_SERVICE=cgroups
DINIT_NO_CONTAINER=1

set -e

. @SCRIPT_PATH@/common.sh

CG_PATH="/sys/fs/cgroup"

mkdir -p "$CG_PATH"
@HELPER_PATH@/mntpt "$CG_PATH" || mount -t cgroup2 -o nsdelegate cgroup2 "/sys/fs/cgroup"

# just in case
[ -e "${CG_PATH}/cgroup.subtree_control" ] || exit 0
[ -e "${CG_PATH}/cgroup.controllers" ] || exit 0

# get the available controllers
read -r CG_ACTIVE < "${CG_PATH}/cgroup.controllers"

# enable them individually; if some fail, that's ok
# we want to enable things here as it may not be possible later
# (e.g. cpu will not enable when there are any rt processes running)
for cont in ${CG_ACTIVE}; do
    echo "+${cont}" > "${CG_PATH}/cgroup.subtree_control" 2>/dev/null || :
done
