#!/bin/sh

DINIT_SERVICE=hostname

. @SCRIPT_PATH@/common.sh

[ -r /etc/hostname ] && read -r HOSTNAME < /etc/hostname
[ -z "$HOSTNAME"   ] && HOSTNAME=chimera

set_hostname() {
    # some container envs allow setting hostname via syscall,
    # but not via procfs; so default to using a command, falling
    # back to procfs when available and when the command is not
    if command -v hostname > /dev/null 2>&1; then
        hostname "$1"
    elif [ -e /proc/sys/kernel/hostname ]; then
        printf "%s" "$1" > /proc/sys/kernel/hostname
    fi
}

# in some environments this may fail
set_hostname "$HOSTNAME" > /dev/null 2>&1 || :
