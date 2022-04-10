#!/bin/sh

. /etc/rc.conf

if [ -z "${container+x}" ]; then
    echo "Seeding random number generator..."
    seedrng || true
fi

echo "Setting up loopback interface..."
ip link set up dev lo

[ -r /etc/hostname ] && read -r HOSTNAME < /etc/hostname
if [ -n "$HOSTNAME" ]; then
    echo "Setting up hostname to '${HOSTNAME}'..."
    printf "%s" "$HOSTNAME" > /proc/sys/kernel/hostname
else
    echo "Didn't setup a hostname!"
fi

if [ -n "$TIMEZONE" ]; then
    echo "Setting up timezone to '${TIMEZONE}'..."
    ln -sf "/usr/share/zoneinfo/$TIMEZONE" /etc/localtime
fi

if [ -x /usr/bin/sysctl ]; then
    echo "Loading sysctl(8) settings..."
    mkdir -p /run/csysctl.d

    for i in /run/sysctl.d/*.conf \
        /etc/sysctl.d/*.conf \
        /usr/local/lib/sysctl.d/*.conf \
        /usr/lib/sysctl.d/*.conf; do

        if [ -e "$i" ] && [ ! -e "/run/csysctl.d/${i##*/}" ]; then
            ln -s "$i" "/run/csysctl.d/${i##*/}"
        fi
    done

    for i in /run/csysctl.d/*.conf; do
        sysctl -p "$i"
    done

    rm -rf -- /run/csysctl.d
    sysctl -p /etc/sysctl.conf
fi

echo "Sanitizing temporary files..."

# ensure X11/xwayland can start rootless
install -d -m 1777 -o root -g root /tmp/.X11-unix /tmp/.ICE-unix

echo "Invoking /etc/rc.local..."

[ -x /etc/rc.local ] && /etc/rc.local

exit 0
