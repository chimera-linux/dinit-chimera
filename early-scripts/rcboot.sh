#!/bin/sh

if [ -z "${container+x}" ]; then
    echo "Seeding random number generator..."
    seedrng || true
fi

echo "Setting up loopback interface..."
ip link set up dev lo

[ -r /etc/hostname ] && read -r HOSTNAME < /etc/hostname
[ -z "$HOSTNAME"   ] && HOSTNAME=chimera

echo "Setting hostname to '${HOSTNAME}'..."
printf "%s" "$HOSTNAME" > /proc/sys/kernel/hostname

if [ -x /usr/bin/sysctl ]; then
    echo "Loading sysctl(8) settings..."
    sysctl --system
fi

echo "Sanitizing temporary files..."

# ensure X11/xwayland can start rootless
install -d -m 1777 -o root -g root /tmp/.X11-unix /tmp/.ICE-unix

echo "Invoking /etc/rc.local..."

[ -x /etc/rc.local ] && /etc/rc.local

exit 0
