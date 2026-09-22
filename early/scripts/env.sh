#!/bin/sh
#
# Expose environment variables in dinit activation environment
#
# This allows early services to work more generically without assumptions

set -e

# passed by the kernel
if [ "$dinit_early_debug" ]; then
    dinitctl --use-passed-cfd setenv "DINIT_EARLY_DEBUG=1"
    # slow execution of each
    if [ -n "$dinit_early_debug_slow" ]; then
        dinitctl --use-passed-cfd setenv "DINIT_EARLY_DEBUG_SLOW=$dinit_early_debug_slow"
    fi
    if [ -n "$dinit_early_debug_log" ]; then
        dinitctl --use-passed-cfd setenv "DINIT_EARLY_DEBUG_LOG=$dinit_early_debug_log"
    fi
fi

# detect if running in a container, expose it globally
if [ -n "${container+x}" ]; then
    dinitctl --use-passed-cfd setenv DINIT_CONTAINER=1
fi

# detect first boot
if [ ! -e /etc/machine-id ]; then
    dinitctl --use-passed-cfd setenv DINIT_FIRST_BOOT=1
elif [ "$(cat /etc/machine-id)" = "uninitialized" ]; then
    dinitctl --use-passed-cfd setenv DINIT_FIRST_BOOT=1
fi

exit 0
