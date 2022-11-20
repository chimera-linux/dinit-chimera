#!/bin/sh

[ -x /usr/bin/sysctl ] || exit 0

/usr/bin/sysctl --system
