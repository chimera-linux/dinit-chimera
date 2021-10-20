#!/bin/sh

[ -x /usr/bin/fsck ] || exit 0

ROOTDEV=`/usr/bin/findmnt -o SOURCE -n -M /`

echo "Checking root file system (^C to skip)..."

/usr/bin/fsck -C -a "$ROOTDEV"
fsckresult=$?

if [ $((fsckresult & 4)) -eq 4 ]; then
    echo "***********************"
    echo "WARNING WARNING WARNING"
    echo "***********************"
    echo "The root file system has problems which require user attention."
    echo "A maintenance shell will now be started; system will then be rebooted."
    /usr/bin/sulogin
    /usr/bin/reboot --use-passed-cfd -r
elif [ $(($fsckresult & 2)) -eq 2 ]; then
    echo "***********************"
    echo "WARNING WARNING WARNING"
    echo "***********************"
    echo "The root file system had problems (now repaired): rebooting..."
    sleep 5
    /usr/bin/reboot --use-passed-cfd -r
fi
