#!/bin/sh

set -e

mountpoint -q /proc || mount -o nosuid,noexec,nodev -t proc     proc /proc
mountpoint -q /sys  || mount -o nosuid,noexec,nodev -t sysfs    sys  /sys
mountpoint -q /dev  || mount -o mode=0755,nosuid    -t devtmpfs dev  /dev

mkdir -p -m0755 /dev/pts /dev/shm

mountpoint -q /dev/pts || mount -o mode=0620,gid=5,nosuid,noexec -n -t devpts devpts /dev/pts
mountpoint -q /dev/shm || mount -o mode=1777,nosuid,nodev -n -t tmpfs shm /dev/shm
mountpoint -q /sys/kernel/security || mount -n -t securityfs securityfs /sys/kernel/security

if [ -d /sys/firmware/efi/efivars ]; then
    mountpoint -q /sys/firmware/efi/efivars || mount -o nosuid,noexec,nodev -t efivarfs efivarfs /sys/firmware/efi/efivars
fi
