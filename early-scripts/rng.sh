#!/bin/sh

[ -z "${container+x}" ] || exit 0

/usr/libexec/seedrng

exit 0
