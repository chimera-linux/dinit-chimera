#!/bin/sh

[ -z "${container+x}" ] || exit 0
/usr/bin/seedrng
exit 0
