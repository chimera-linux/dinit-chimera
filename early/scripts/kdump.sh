#!/bin/sh
# inspired by void runit-kdump

DINIT_SERVICE=kdump
DINIT_NO_CONTAINER=1

set -e

. @SCRIPT_PATH@/common.sh

# this is optional functionality
command -v makedumpfile > /dev/null 2>&1 || exit 0
command -v vmcore-dmesg > /dev/null 2>&1 || exit 0
command -v kexec > /dev/null 2>&1 || exit 0

if [ -e /proc/vmcore ] && ! grep -q nokdump /proc/cmdline; then
    DUMP_DIR="/var/crash/kdump-$(date +%Y%m%d-%H%M%S)"
    # save vmcore
    echo "Saving vmcore to '$DUMP_DIR'..."
    mkdir -p "$DUMP_DIR"
    makedumpfile -l --message-level 1 -d 31 /proc/vmcore "${DUMP_DIR}/vmcore.tmp" \
        && mv "${DUMP_DIR}/vmcore.tmp" "${DUMP_DIR}/vmcore"
    # save dmesg
    echo "Saving dmesg to '$DUMP_DIR'..."
    vmcore-dmesg /proc/vmcore > "${DIR}/dmesg.txt.tmp" \
        && mv "${DUMP_DIR}/dmesg.txt.tmp" "${DUMP_DIR}/dmesg.txt"
    sync
    # force reboot after saving
    echo "Crash dump done, rebooting..."
    sleep 5
    reboot --use-passed-cfd -r
    exit 0
fi

# crashkernel=NNN not specified (default), silently succeed
if [ "$(cat /sys/kernel/kexec_crash_size)" = "0" ]; then
    exit 0
fi

KERNVER=$(uname -r)

# try determining the kernel image path in a semi-generic way...
if command -v linux-version > /dev/null 2>&1; then
    # we have linux-version? great, then it's nice and easy
    KERNIMG=$(linux-version list --paths | grep "^$KERNVER" | cut -d ' ' -f2)
else
    # scuffed but probably generic enough detection...
    for kern in /boot/vmlinu*${KERNVER} /boot/*Image*${KERNVER}; do
        [ -e "$kern" ] || continue
        KERNIMG="$kern"
        break
    done
fi

if [ -z "$KERNIMG" ]; then
    echo "WARNING: could not determine kernel image path for '${KERNVER}', skipping loading crash kernel..."
    exit 0
fi

# now do that for initramfs, we have no tooling we could use for that
# we may have a dedicated kdump initramfs so try matching these first
for rd in /boot/initr*${KERNVER}*kdump* /boot/initr*${KERNVER}*; do
    [ -e "$rd" ] || continue
    INITRAMFS="$rd"
    break
done

if [ -z "$INITRAMFS" ]; then
    echo "WARNING: could not find initramfs for '${KERNVER}', skipping initramfs loading..."
fi

# may need adjusting
KAPPEND="irqpoll nr_cpus=1 maxcpus=1 reset_devices udev.children-max=2 panic=10 cgroup_disable=memory mce=off numa=off"

echo "Loading crash kernel '${KERNIMG}'..."
exec kexec --load-panic "$KERNIMG" ${INITRAMFS:+--initrd="${INITRAMFS}"} \
    --reuse-cmdline --append="${KAPPEND}"
