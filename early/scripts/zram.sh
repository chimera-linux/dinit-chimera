#!/bin/sh

DINIT_SERVICE=zram
DINIT_NO_CONTAINER=1

set -eu

# default settings
RAM_PERCENTAGE=100
MAX_SIZE_MB=8192
ALGORITHM=lzo-rle
[ -f "/etc/default/zram" ] && . /etc/default/zram

STATUS_FILE=/run/zram-swap-device

get_total_memory_kb() {
    grep MemTotal: /proc/meminfo | cut -w -f2
}

calculate_swap_size_kb() {
    total_memory_kb=$(get_total_memory_kb)
    a=$(( total_memory_kb * RAM_PERCENTAGE / 100 ))
    b=$(( MAX_SIZE_MB * 1024 ))
    echo $(( a < b ? a : b ))
}

stop() {
    dev="$(cat $STATUS_FILE)"
    swapoff "$dev" || true
    zramctl --reset "$dev"
    rm "$STATUS_FILE"
}

start() {
    modprobe zram
    size="$(calculate_swap_size_kb)K"
    dev=$(zramctl --find --size "$size" --algorithm "$ALGORITHM")
    echo "$dev" > "$STATUS_FILE"
    mkswap -U clear "$dev"
    swapon --priority 100 "$dev"
}

case "$@" in
    start)
        start
        ;;
    stop)
        [ -f "$STATUS_FILE" ] && stop
        ;;
    *)
        echo "Usage: $0 start|stop"
esac
