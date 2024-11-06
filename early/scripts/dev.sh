#!/bin/sh

case "$1" in
    start|stop) DINIT_SERVICE=dev ;;
    trigger|settle) DINIT_SERVICE="dev-$1" ;;
    *) DINIT_SERVICE=dev-unknown ;;
esac

DINIT_NO_CONTAINER=1

. @SCRIPT_PATH@/common.sh

exec @DINIT_DEVD_PATH@ "$1"
