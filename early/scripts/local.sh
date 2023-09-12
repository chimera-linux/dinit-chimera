#!/bin/sh

DINIT_SERVICE=local

. ./early/scripts/common.sh

[ -x /etc/rc.local ] && /etc/rc.local

exit 0
