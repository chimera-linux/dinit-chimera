#!/bin/sh

[ -z "${container+x}" ] || exit 0
[ -x /usr/bin/dmraid  ] || exit 0

/usr/bin/dmraid -i -ay
