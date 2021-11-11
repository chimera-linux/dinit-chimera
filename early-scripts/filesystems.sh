#!/bin/sh

. /usr/libexec/dinit/early/common.sh

echo "Remounting rootfs read-only..."
mount -o remount,ro / || exit 1

echo "Mounting early filesystems..."

# proc, sys, dev, run
mountpoint -q /proc || mount -o nosuid,noexec,nodev    -t proc     proc /proc
mountpoint -q /sys  || mount -o nosuid,noexec,nodev    -t sysfs    sys  /sys
mountpoint -q /dev  || mount -o mode=0755,nosuid       -t devtmpfs dev  /dev
mountpoint -q /run  || mount -o mode=0755,nosuid,nodev -t tmpfs    run  /run

# core directories
mkdir -p -m0755 /run/lvm /run/user /run/lock /run/log /dev/pts /dev/shm

# other core mounts
mountpoint -q /dev/pts || mount -o mode=0620,gid=5,nosuid,noexec -n -t devpts devpts /dev/pts
mountpoint -q /dev/shm || mount -o mode=1777,nosuid,nodev -n -t tmpfs shm /dev/shm
mountpoint -q /sys/kernel/security || mount -n -t securityfs securityfs /sys/kernel/security

is_container && exit 0

mkdir -p "/sys/fs/cgroup"
mountpoint -q "/sys/fs/cgroup" || mount -t cgroup2 -o nsdelegate cgroup2 "/sys/fs/cgroup"
