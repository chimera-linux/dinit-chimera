#!/bin/sh

[ -x "@SCRIPT_PATH@/kdump.sh" ] || exit 0

exec @SCRIPT_PATH@/kdump.sh "$@"
