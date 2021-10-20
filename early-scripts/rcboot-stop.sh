#!/bin/sh

# The system is being shut down

echo "Saving random number seed..."
POOLSIZE="$(cat /proc/sys/kernel/random/poolsize)"

dd if=/dev/urandom of=/var/state/random-seed bs="$POOLSIZE" count=1 2> /dev/null
