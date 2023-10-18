#!/bin/sh

DINIT_SERVICE=binfmt
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

if [ "$1" = "stop" ]; then
   exec ./early/helpers/binfmt -u
fi

# require the module if it's around, but don't fail - it may be builtin
modprobe -bq binfmt_misc 2> /dev/null

# try to make sure it's mounted too, otherwise binfmt-helper will fail
./early/helpers/mntpt /proc/sys/fs/binfmt_misc || mount -o nosuid,noexec,nodev \
    -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null

exec ./early/helpers/binfmt
