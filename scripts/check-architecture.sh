#!/usr/bin/env bash
set -euo pipefail

failed=0

report_matches() {
    local description=$1
    local directory=$2
    local pattern=$3

    [[ -d "$directory" ]] || return 0
    if grep -RInE --include='*.cpp' --include='*.hpp' "$pattern" "$directory"; then
        echo "architecture: $description" >&2
        failed=1
    fi
}

report_matches "core may depend only on GLib" src/core \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|sigc\+\+/|job/|fs/|mon/|nav/|dash/|diff/|his/|search/|trash/|ui/)'
report_matches "dash may depend only on core and GLib" src/dash \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|job/|fs/|mon/|nav/|ui/)'
report_matches "job must not depend on GTK, fs, mon, nav, dash, or ui" src/job \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|fs/|mon/|nav/|dash/|ui/)'
report_matches "fs must not depend on GTK, mon, nav, dash, or ui" src/fs \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|mon/|nav/|dash/|ui/)'
report_matches "mon must not depend on GTK, fs, job, nav, dash, or ui" src/mon \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|fs/|job/|nav/|dash/|ui/)'
report_matches "nav must not depend on GTK, fs, job, mon, dash, or ui" src/nav \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|fs/|job/|mon/|dash/|ui/)'
report_matches "diff may depend only on core and GLib" src/diff \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|gdk-pixbuf/|job/|fs/|mon/|nav/|dash/|his/|search/|trash/|ui/)'
report_matches "his may depend only on core, fs, GLib, and SQLite" src/his \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|job/|mon/|nav/|dash/|diff/|search/|trash/|ui/)'
report_matches "search may depend only on core and GLib" src/search \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|job/|fs/|mon/|nav/|dash/|his/|diff/|trash/|ui/)'
report_matches "trash may depend only on core, job, fs, and GLib" src/trash \
    '^[[:space:]]*#include[[:space:]]*[<"]((gtk|gdk)(mm)?([/.]|[>"])|mon/|nav/|dash/|his/|diff/|search/|ui/)'

while IFS= read -r file; do
    [[ "$file" == src/his/* ]] && continue
    echo "$file: architecture: only src/his may include sqlite3.h" >&2
    failed=1
done < <(grep -RlE --include='*.cpp' --include='*.hpp' \
    '^[[:space:]]*#include[[:space:]]*[<"]sqlite3\.h[>"]' src 2>/dev/null)

while IFS= read -r file; do
    while IFS= read -r include; do
        [[ -n "$include" ]] || continue
        if [[ "$include" == libayatana-appindicator/* && "$file" != src/ui/* ]]; then
            echo "$file: architecture: only src/ui may include libayatana-appindicator headers" >&2
            failed=1
        fi
        if [[ "$include" =~ ^([^/]+)/.*_internal\.hpp$ ]]; then
            owner=${BASH_REMATCH[1]}
            caller=${file#*/}
            caller=${caller%%/*}
            if [[ "$caller" != "$owner" ]]; then
                echo "$file: architecture: internal header belongs to $owner" >&2
                failed=1
            fi
        fi
        if [[ "$include" =~ _testing\.hpp$ && "$file" != tests/* ]]; then
            echo "$file: architecture: testing headers are tests-only" >&2
            failed=1
        fi
    done < <(sed -nE 's/^[[:space:]]*#include[[:space:]]*[<"]([^>"]+)[>"].*/\1/p' "$file")
done < <(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print 2>/dev/null)

if ((failed)); then
    exit 1
fi
echo "architecture: PASS"
