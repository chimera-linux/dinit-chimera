#!/bin/sh

[ -z "${container+x}" ] || exit 0
[ -x /usr/bin/btrfs   ] || exit 0

/usr/bin/btrfs device scan
