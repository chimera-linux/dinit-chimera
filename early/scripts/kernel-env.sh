#!/bin/sh
#
# Expose kernel environment in dinit
#
# It may be cleared by early init, so re-parse it from procfs

DINIT_SERVICE=kernel-env
# containers do not clear environment so no need, also not portable
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

set -e

[ -r /proc/cmdline ] || exit 0

# ensures quoting is safe and so on
eval set -- $(cat /proc/cmdline)

for enval in "$@"; do
    case "$enval" in
        -) break ;;
        *=*) dinitctl --use-passed-cfd setenv "$enval" ;;
    esac
done

exit 0
