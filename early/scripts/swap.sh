#!/bin/sh

DINIT_SERVICE=swap
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

exec swapon -a
