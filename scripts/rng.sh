#!/bin/sh

[ -z "${container+x}" ] || exit 0

/usr/libexec/dinit/helpers/seedrng

exit 0
