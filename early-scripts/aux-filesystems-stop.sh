#!/bin/sh

# container environment
[ -z "${container+x}" ] || exit 0

echo "Unmounting filesystems, disabling swap..."
swapoff -a
umount -r -a -t nosysfs,noproc,nodevtmpfs,notmpfs

echo "Remounting rootfs read-only..."
mount -o remount,ro /

deactivate_vgs() {
   _group=${1:-All}
   if [ -x /usr/bin/vgchange ]; then
       vgs=$(vgs|wc -l)
       if [ $vgs -gt 0 ]; then
           echo "Deactivating $_group LVM Volume Groups..."
           vgchange -an
       fi
   fi
}

deactivate_crypt() {
   if [ -x /usr/bin/dmsetup ]; then
       echo "Deactivating Crypt Volumes"
       for v in $(dmsetup ls --target crypt --exec "dmsetup info -c --noheadings -o open,name"); do
           [ ${v%%:*} = "0" ] && cryptsetup close ${v##*:}
       done
       deactivate_vgs "Crypt"
   fi
}

deactivate_vgs
deactivate_crypt
