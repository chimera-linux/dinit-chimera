#!/bin/sh

[ -z "${container+x}" ] || exit 0
[ -x /usr/bin/zfs     ] || exit 0
[ -x /usr/bin/zpool   ] || exit 0

if [ -e /etc/zfs/zpool.cache ]; then
    zpool import -N -a -c /etc/zfs/zpool.cache
else
    zpool import -N -a -o cachefile=none
fi

zfs mount -a -l
zfs share -a
