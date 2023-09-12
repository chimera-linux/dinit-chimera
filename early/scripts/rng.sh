#!/bin/sh

DINIT_SERVICE=rng
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

exec ./early/helpers/seedrng
