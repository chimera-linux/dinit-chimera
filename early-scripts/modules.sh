#!/bin/sh

# no modules support
[ -e /proc/modules ] || exit 0

# no modules file
[ -r /etc/modules ] || exit 0

# container environment
[ -z "${container+x}" ] || exit 0

echo "Loading kernel modules..."

modules-load -v | tr '\n' ' ' | sed 's:insmod [^ ]*/::g; s:\.ko\(\.gz\)\? ::g'

echo
