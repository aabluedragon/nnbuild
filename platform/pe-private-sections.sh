#!/bin/sh
# Print objcopy options that give one game its own copy of shared-by-name sections.
#
# Usage: pe-private-sections.sh <objdump> <game> <hide-list> <prelinked-object>
#
# Windows (PE/COFF) objects put every inline data item (virtual tables, the
# ".refptr" pointer stubs used on x64, and so on) in its own "link once" section
# named <kind>$<symbol>. The final link keeps one section per name and drops the
# rest, which is right for identical copies but wrong when two games linked into
# the same executable each have their own, different item under the same name.
# For every symbol that the merge step is about to make private to <game>, this
# renames its link-once section to <kind>$<game>.<symbol> so the linker keeps
# it instead of substituting another game's copy.
set -e
objdump="$1"
game="$2"
hide="$3"
object="$4"

"$objdump" -h "$object" | awk -v game="$game" -v hidefile="$hide" '
BEGIN {
    while ((getline line < hidefile) > 0)
        hidden[line] = 1
}
/^ *[0-9]+ / { name = $2; next }
/LINK_ONCE/ && name != "" {
    split_at = index(name, "$")
    if (split_at) {
        symbol = substr(name, split_at + 1)
        # 32-bit x86 symbol names carry a leading underscore; section names do not.
        if ((symbol in hidden) || (("_" symbol) in hidden))
            printf "--rename-section %s=%s$%s.%s\n", name, substr(name, 1, split_at - 1), game, symbol
    }
    name = ""
}
'
