#!/bin/sh

if [ -z "${container+x}" ]; then
    echo "Saving random number seed..."
    seedrng
fi
