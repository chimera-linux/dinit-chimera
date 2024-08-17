#!/bin/sh

DINIT_SERVICE=modules-early
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

exec ./early/helpers/kmod static-modules
