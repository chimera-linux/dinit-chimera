#!/bin/sh

. /etc/rc.conf

if [ -z "${container+x}" ]; then
    echo "Saving random number seed..."
    export SEEDRNG_SKIP_CREDIT
    seedrng
fi
