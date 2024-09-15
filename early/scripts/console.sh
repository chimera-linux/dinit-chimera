#!/bin/sh

DINIT_SERVICE=${1:-console}
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

[ -x /usr/libexec/dinit-console ] || exit 0

exec /usr/libexec/dinit-console "$1"
