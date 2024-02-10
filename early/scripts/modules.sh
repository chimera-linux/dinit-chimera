#!/bin/sh

DINIT_SERVICE=modules
DINIT_NO_CONTAINER=1

. ./early/scripts/common.sh

# no modules support
[ -e /proc/modules ] || exit 0

{
    # Parameters passed as modules-load= or modules_load= in kernel command line.
    sed -nr 's/,/\n/g;s/(.* |^)modules[-_]load=([^ ]*).*/\2/p' /proc/cmdline

    # Find files /{etc,run,usr/lib}/modules-load.d/*.conf in that order.
    find -L /etc/modules-load.d /run/modules-load.d /usr/lib/modules-load.d \
        -maxdepth 1 -name '*.conf' 2>/dev/null | sed 's,.*/\(.*\),& \1,' |
        # Load each basename only once.
        sort -k2 -s | uniq -f1 | cut -d' ' -f1 |
        # Read the files, output all non-empty, non-comment lines.
        tr '\012' '\0' | xargs -0 grep -h -v -e '^[#;]' -e '^$'
} |
# Call modprobe on the list of modules
tr '\012' '\0' | xargs -0 modprobe -ab
