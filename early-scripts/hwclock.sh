#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

. /etc/rc.conf

if [ -n "$HARDWARECLOCK" ]; then
    echo "Setting up RTC to '${HARDWARECLOCK}'..."
    hwclock --systz \
        ${HARDWARECLOCK:+--$(echo $HARDWARECLOCK |tr A-Z a-z) --noadjfile} || exit 1
fi
