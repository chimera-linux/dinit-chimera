#!/bin/sh

DINIT_SERVICE=sysctl
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

exec ./early/helpers/sysctl
