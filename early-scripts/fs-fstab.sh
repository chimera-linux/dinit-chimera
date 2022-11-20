#!/bin/sh

[ -z "${container+x}" ] || exit 0

case "$1" in
    start)
        /usr/bin/mount -a -t "nosysfs,nonfs,nonfs4,nosmbfs,nocifs" -O no_netdev
        ;;
    stop)
        /usr/bin/umount -r -a -t nosysfs,noproc,nodevtmpfs,notmpfs
        ;;
     *) exit 1 ;;
esac
