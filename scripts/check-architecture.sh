#!/usr/bin/env bash
set -euo pipefail

failed=0

report_matches() {
    local description=$1
    local directory=$2
    local pattern=$3

    [[ -d "$directory" ]] || return 0
    if grep -RInE --include='*.c' --include='*.h' "$pattern" "$directory"; then
        echo "architecture: $description" >&2
        failed=1
    fi
}

report_matches "core may depend only on GLib" src/core \
    '^[[:space:]]*#include[[:space:]]*[<"](gtk|job|fs|mon|nav|dash|ui)/'
report_matches "dash may depend only on core and GLib" src/dash \
    '^[[:space:]]*#include[[:space:]]*[<"](gtk|job|fs|mon|nav|ui)/'
report_matches "job must not depend on GTK, fs, mon, nav, dash, or ui" src/job \
    '^[[:space:]]*#include[[:space:]]*[<"](gtk|fs|mon|nav|dash|ui)/'
report_matches "fs must not depend on GTK, mon, nav, dash, or ui" src/fs \
    '^[[:space:]]*#include[[:space:]]*[<"](gtk|mon|nav|dash|ui)/'
report_matches "mon must not depend on GTK, fs, job, nav, dash, or ui" src/mon \
    '^[[:space:]]*#include[[:space:]]*[<"](gtk|fs|job|nav|dash|ui)/'
report_matches "nav must not depend on GTK, fs, job, mon, dash, or ui" src/nav \
    '^[[:space:]]*#include[[:space:]]*[<"](gtk|fs|job|mon|dash|ui)/'

while IFS= read -r file; do
    [[ "$file" == src/his/* ]] && continue
    echo "$file: architecture: only src/his may include sqlite3.h" >&2
    failed=1
done < <(grep -RlE --include='*.c' --include='*.h' \
    '^[[:space:]]*#include[[:space:]]*[<"]sqlite3\.h[>"]' src 2>/dev/null)

while IFS= read -r file; do
    while IFS= read -r include; do
        [[ -n "$include" ]] || continue
        if [[ "$include" =~ ^([^/]+)/.*_internal\.h$ ]]; then
            owner=${BASH_REMATCH[1]}
            caller=${file#*/}
            caller=${caller%%/*}
            if [[ "$caller" != "$owner" ]]; then
                echo "$file: architecture: internal header belongs to $owner" >&2
                failed=1
            fi
        fi
        if [[ "$include" =~ _testing\.h$ && "$file" != tests/* ]]; then
            echo "$file: architecture: testing headers are tests-only" >&2
            failed=1
        fi
    done < <(sed -nE 's/^[[:space:]]*#include[[:space:]]*[<"]([^>"]+)[>"].*/\1/p' "$file")
done < <(find src tests -type f \( -name '*.c' -o -name '*.h' \) -print 2>/dev/null)

if ((failed)); then
    exit 1
fi
echo "architecture: PASS"
