#!/bin/sh

DINIT_SERVICE=pseudofs
# can't mount in containers
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

set -e

@HELPER_PATH@/mnt try /proc proc proc nosuid,noexec,nodev

# remount root after we have procfs
mount -o remount,${dinit_early_root_remount:-ro,rshared} /

# then do the rest of the pseudofs shenanigans
@HELPER_PATH@/mnt try /sys sys sysfs nosuid,noexec,nodev
@HELPER_PATH@/mnt try /dev dev devtmpfs mode=0755,nosuid

mkdir -p -m0755 /dev/pts /dev/shm

# provide a fallback in case of failure
TTY_ENT=$(getent group tty 2>/dev/null) || TTY_ENT="tty:x:5"

@HELPER_PATH@/mnt try /dev/pts devpts devpts mode=0620,gid=$(echo $TTY_ENT | cut -d: -f3),nosuid,noexec
@HELPER_PATH@/mnt try /dev/shm shm tmpfs mode=1777,nosuid,nodev

[ -h /dev/fd ] || ln -s /proc/self/fd /dev/fd
[ -h /dev/stdin ] || ln -s /proc/self/fd/0 /dev/stdin
[ -h /dev/stdout ] || ln -s /proc/self/fd/1 /dev/stdout
[ -h /dev/stderr ] || ln -s /proc/self/fd/2 /dev/stderr

if [ -d /sys/kernel/security ]; then
    @HELPER_PATH@/mnt try /sys/kernel/security securityfs securityfs
fi

if [ -d /sys/firmware/efi/efivars ]; then
    @HELPER_PATH@/mnt try /sys/firmware/efi/efivars efivarfs efivarfs nosuid,noexec,nodev
fi

if [ -d /sys/fs/selinux ]; then
    @HELPER_PATH@/mnt try /sys/fs/selinux selinuxfs selinuxfs
fi
