#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

[ -e /run/dinit/container ] && exit 0

case "$1" in
    start)
        mount -a -t "nosysfs,nonfs,nonfs4,nosmbfs,nocifs" -O no_netdev
        ;;
    stop)
        umount -r -a -t nosysfs,noproc,nodevtmpfs,notmpfs
        ;;
     *) exit 1 ;;
esac
