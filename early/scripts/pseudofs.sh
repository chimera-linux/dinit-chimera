#!/bin/sh

DINIT_SERVICE=pseudofs
# can't mount in containers
DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec @HELPER_PATH@/mnt prepare ${dinit_early_root_remount:-ro,rshared}
