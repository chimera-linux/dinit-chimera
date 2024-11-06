#!/bin/sh

DINIT_SERVICE=local

. @SCRIPT_PATH@/common.sh

[ -x /etc/rc.local ] && /etc/rc.local

exit 0
