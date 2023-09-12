#!/bin/sh
#
# Common code sourced into each early boot script.

# sanitize common PATH
export PATH=/sbin:/bin:/usr/sbin:/usr/bin

# convenience debug logging function
log_debug() {
    [ -n "$DINIT_EARLY_DEBUG" ] || return 0
    echo "INIT:" "$@"
    [ -n "$DINIT_EARLY_DEBUG_SLOW" ] && sleep "$DINIT_EARLY_DEBUG_SLOW"
}

# if requested, append all to logfile
if [ -n "$DINIT_EARLY_DEBUG" -a -n "$DINIT_EARLY_DEBUG_LOG" ]; then
    exec 1>>"$DINIT_EARLY_DEBUG_LOG"
    exec 2>&1
fi

[ -z "$DINIT_CONTAINER" -o -z "$DINIT_NO_CONTAINER" ] || exit 0

log_debug "$DINIT_SERVICE"
