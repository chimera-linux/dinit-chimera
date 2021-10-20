#!/bin/sh

is_container() {
    [ -e /proc/self/environ ] && return 1
    grep -q lxc /proc/self/environ > /dev/null
}
