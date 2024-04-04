#!/bin/sh

DINIT_SERVICE=mdadm
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

command -v mdadm > /dev/null 2>&1 || exit 0

CONFIG=/etc/mdadm.conf
ALTCONFIG=/etc/mdadm/mdadm.conf

[ ! -f "$CONFIG" ] && [ -f "$ALTCONFIG" ] && CONFIG="$ALTCONFIG" || :

# no config
if [ ! -f "$CONFIG" ]; then
    exit 0
fi

# no array in config
if ! grep -q "^ARRAY" "$CONFIG"; then
    exit 0
fi

exec mdadm -As
