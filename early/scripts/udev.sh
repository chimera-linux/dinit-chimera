#!/bin/sh

case "$1" in
    start|stop) DINIT_SERVICE=udev ;;
    trigger|settle) DINIT_SERVICE="udev-$1" ;;
    *) DINIT_SERVICE=udev-unknown ;;
esac

DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

case "$1" in
    start) exec /usr/libexec/udevd --daemon ;;
    stop) udevadm control -e || : ;;
    settle) exec udevadm settle ;;
    trigger) exec udevadm trigger --action=add ;;
    *) exit 1 ;;
esac
