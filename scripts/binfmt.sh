#!/bin/sh

export PATH=/usr/bin

# container environment
[ -z "${container+x}" ] || exit 0

if [ "$1" = "stop" ]; then
   exec /usr/libexec/binfmt-helper -u
fi

# require the module if it's around, but don't fail - it may be builtin
modprobe -bq binfmt_misc 2> /dev/null

# try to make sure it's mounted too, otherwise binfmt-helper will fail
mountpoint -q /proc/sys/fs/binfmt_misc || mount -o nosuid,noexec,nodev \
    -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null

exec /usr/libexec/dinit/helpers/binfmt
