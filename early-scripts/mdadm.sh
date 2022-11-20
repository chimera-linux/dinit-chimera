#!/bin/sh

[ -z "${container+x}" ] || exit 0
[ -x /usr/bin/mdadm   ] || exit 0

/usr/bin/mdadm -As
