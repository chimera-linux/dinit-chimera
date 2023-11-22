#!/bin/sh

DINIT_SERVICE=pseudofs
# can't mount in containers
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

set -e

mntpt() {
    ./early/helpers/mntpt "$@"
}

mntpt /proc || mount -o nosuid,noexec,nodev -t proc     proc /proc
mntpt /sys  || mount -o nosuid,noexec,nodev -t sysfs    sys  /sys
mntpt /dev  || mount -o mode=0755,nosuid    -t devtmpfs dev  /dev

mkdir -p -m0755 /dev/pts /dev/shm

mntpt /dev/pts || mount -o mode=0620,gid=5,nosuid,noexec -n -t devpts devpts /dev/pts
mntpt /dev/shm || mount -o mode=1777,nosuid,nodev -n -t tmpfs shm /dev/shm

[ -h /dev/fd ] || ln -s /proc/self/fd /dev/fd
[ -h /dev/stdin ] || ln -s /proc/self/fd/0 /dev/stdin
[ -h /dev/stdout ] || ln -s /proc/self/fd/1 /dev/stdout
[ -h /dev/stderr ] || ln -s /proc/self/fd/2 /dev/stderr

if [ -d /sys/kernel/security ]; then
    mntpt /sys/kernel/security || mount -n -t securityfs securityfs /sys/kernel/security
fi

if [ -d /sys/firmware/efi/efivars ]; then
    mntpt /sys/firmware/efi/efivars || mount -o nosuid,noexec,nodev -t efivarfs efivarfs /sys/firmware/efi/efivars
fi
