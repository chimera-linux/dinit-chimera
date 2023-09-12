#!/bin/sh
#
# TODO: actually handle errors properly

DINIT_SERVICE=fs-zfs
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v zfs > /dev/null 2>&1 || exit 0
command -v zpool > /dev/null 2>&1 || exit 0

if [ -e /etc/zfs/zpool.cache ]; then
    zpool import -N -a -c /etc/zfs/zpool.cache || exit 0
else
    zpool import -N -a -o cachefile=none || exit 0
fi

zfs mount -a -l || exit 0
zfs share -a || :
