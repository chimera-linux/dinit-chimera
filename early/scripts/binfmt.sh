#!/bin/sh

DINIT_SERVICE=binfmt
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

if [ "$1" = "stop" ]; then
   exec @HELPER_PATH@/binfmt -u
fi

# require the module if it's around, but don't fail - it may be builtin
@HELPER_PATH@/kmod load binfmt_misc

# try to make sure it's mounted too, otherwise binfmt-helper will fail
@HELPER_PATH@/mnt try /proc/sys/fs/binfmt_misc binfmt_misc binfmt_misc \
    nosuid,noexec,nodev 2>/dev/null

exec @HELPER_PATH@/binfmt
