#!/bin/sh

[ -x "./early/scripts/kdump.sh" ] || exit 0

exec ./early/scripts/kdump.sh "$@"
