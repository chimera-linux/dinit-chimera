#!/bin/sh

DINIT_SERVICE=fs-fstab
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

case "$1" in
    start)
        exec mount -a -t "nosysfs,nonfs,nonfs4,nosmbfs,nocifs" -O no_netdev
        ;;
    stop)
        exec umount -r -a -t nosysfs,noproc,nodevtmpfs,notmpfs
        ;;
     *) exit 1 ;;
esac
