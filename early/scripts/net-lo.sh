#!/bin/sh

DINIT_SERVICE=net-lo

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/lo
