#!/bin/sh

[ -x /usr/bin/setupcon ] || exit 0

exec /usr/bin/setupcon "$@"
