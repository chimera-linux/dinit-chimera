#!/bin/sh

DINIT_SERVICE=tmpfs

. ./early/scripts/common.sh

umask 022
set -e

# default unset
RUNSIZE=

# if initramfs-tools is used, source its configs for consistent runsize
if [ -r /etc/initramfs-tools/initramfs.conf ]; then
    . /etc/initramfs-tools/initramfs.conf
    for conf in /etc/initramfs-tools/conf.d/*; do
        [ -f "$conf" ] && . "$conf"
    done
fi

# overrides via kernel cmdline
if [ -r /proc/cmdline ]; then
    for x in $(cat /proc/cmdline); do
        case "$x" in
            # initramfs-tools compat
            initramfs.runsize=*)
                RUNSIZE="${x#initramfs.runsize=}"
                ;;
            dinit.runsize=*)
                RUNSIZE="${x#dinit.runsize=}"
                ;;
        esac
    done
fi

RUNSIZE="${RUNSIZE:-10%}"

./early/helpers/mntpt /run || \
    mount -o "nodev,noexec,nosuid,size=${RUNSIZE},mode=0755" -t tmpfs tmpfs /run

# readable system state
mkdir -p /run/dinit /run/user

# mount /run/user at this point, should *not* be noexec (breaks some flatpaks)
# give it the same max size as /run itself, generally it should be tiny so
# it does not need the 50% default at any point
./early/helpers/mntpt /run/user || \
    mount -o "nodev,nosuid,size=${RUNSIZE},mode=0755" -t tmpfs tmpfs /run/user

# now that we a /run, expose container as state file too (for shutdown etc)
if [ -n "$DINIT_CONTAINER" ]; then
    touch /run/dinit/container
fi

# ditto
if [ -n "$DINIT_FIRST_BOOT" ]; then
    touch /run/dinit/first-boot
fi
