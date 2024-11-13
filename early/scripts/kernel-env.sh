#!/bin/sh
#
# Expose kernel environment in dinit
#
# Nothing to do here for now, as there is no way to tell what would
# become environment variables.

DINIT_SERVICE=kernel-env
# containers do not clear environment so no need, also not portable
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

set -e

exit 0
