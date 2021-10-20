#!/bin/sh

. /etc/dinit.d/early-scripts/common.sh

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

# cgroup mounts
_cgroupv1="/sys/fs/cgroup"
_cgroupv2="${_cgroupv1}/unified"

# cgroup v1
mountpoint -q "$_cgroupv1" || mount -o mode=0755 -t tmpfs cgroup "$_cgroupv1"
while read -r _subsys_name _hierarchy _num_cgroups _enabled; do
    [ "$_enabled" = "1" ] || continue
    _controller="${_cgroupv1}/${_subsys_name}"
    mkdir -p "$_controller"
    mountpoint -q "$_controller" || mount -t cgroup -o "$_subsys_name" cgroup "$_controller"
done < /proc/cgroups

# cgroup v2
mkdir -p "$_cgroupv2"
mountpoint -q "$_cgroupv2" || mount -t cgroup2 -o nsdelegate cgroup2 "$_cgroupv2"
