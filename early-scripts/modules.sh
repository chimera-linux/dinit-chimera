#!/bin/sh

. /etc/dinit.d/early-scripts/common.sh

# no modules support
[ -e /proc/modules ] || exit 0

# no modules file
[ -r /etc/modules ] || exit 0

# lxc containers
is_container && exit 0

echo "Loading kernel modules..."

modules-load -v | tr '\n' ' ' | sed 's:insmod [^ ]*/::g; s:\.ko\(\.gz\)\? ::g'

echo
