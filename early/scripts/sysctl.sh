#!/bin/sh

DINIT_SERVICE=sysctl

. ./early/scripts/common.sh

exec ./early/helpers/sysctl
