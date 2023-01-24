#!/bin/sh

[ -z "${container+x}" ] || exit 0
[ -x /usr/bin/zfs     ] || exit 0
[ -x /usr/bin/zpool   ] || exit 0

if [ -e /etc/zfs/zpool.cache ]; then
    zpool import -N -a -c /etc/zfs/zpool.cache || exit 0
else
    zpool import -N -a -o cachefile=none || exit 0
fi

zfs mount -a -l || exit 0
zfs share -a || :
