#!/bin/sh

. /usr/libexec/dinit/early/common.sh

is_container && exit 0

. /etc/rc.conf

TTYS=${TTYS:-12}
if [ -n "$FONT" ]; then
    echo "Setting up TTYs font to '${FONT}'..."

    _index=0
    while [ ${_index} -le $TTYS ]; do
        setfont ${FONT_MAP:+-m $FONT_MAP} ${FONT_UNIMAP:+-u $FONT_UNIMAP} \
                $FONT -C "/dev/tty${_index}"
        _index=$((_index + 1))
    done
fi

if [ -n "$KEYMAP" ]; then
    echo "Setting up keymap to '${KEYMAP}'..."
    loadkeys -q -u ${KEYMAP}
fi
