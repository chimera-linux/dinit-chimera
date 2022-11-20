#!/bin/sh

# ensure X11/xwayland can start rootless
install -d -m 1777 -o root -g root /tmp/.X11-unix /tmp/.ICE-unix

[ -x /etc/rc.local ] && /etc/rc.local

exit 0
