#!/bin/sh

[ -e /run/dinit/container ] && exit 0

/usr/libexec/dinit/helpers/seedrng

exit 0
