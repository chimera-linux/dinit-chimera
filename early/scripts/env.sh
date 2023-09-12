#!/bin/sh
#
# Expose environment variables in dinit activation environment
#
# This allows early services to work more generically without assumptions

set -e

# passed by the kernel
if [ "$dinit_early_debug" ]; then
    dinitctl setenv "DINIT_EARLY_DEBUG=1"
    # slow execution of each
    if [ -n "$dinit_early_debug_slow" ]; then
        dinitctl setenv "DINIT_EARLY_DEBUG_SLOW=$dinit_early_debug_slow"
    fi
    if [ -n "$dinit_early_debug_log" ]; then
        dinitctl setenv "DINIT_EARLY_DEBUG_LOG=$dinit_early_debug_log"
    fi
fi

exit 0
