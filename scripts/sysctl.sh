#!/bin/sh

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

[ -e /run/dinit/container ] && exit 0
command -v sysctl > /dev/null 2>&1 || exit 0

exec sysctl --system
