#!/bin/sh

DINIT_SERVICE=rng
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/seedrng
