#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

. /etc/rc.conf

if [ -n "$HARDWARECLOCK" ]; then
    hwclock --systohc ${HARDWARECLOCK:+--$(echo $HARDWARECLOCK |tr A-Z a-z)}
fi
