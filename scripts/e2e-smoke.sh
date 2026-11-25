#!/usr/bin/env bash
set -euo pipefail

fixture=$(mktemp -d)
app=$1
cleanup() {
    if [[ -d "$fixture/locked" ]]; then
        chmod 700 "$fixture/locked"
    fi
    rm -rf -- "$fixture"
}
trap cleanup EXIT

screenshots=${FILESCOPE_SCREENSHOT_DIR:-"$(pwd)/build/ui-screenshots"}
mkdir -p "$screenshots" "$fixture/default" "$fixture/empty" "$fixture/xdg-config" "$fixture/xdg-data"
printf 'FileScope\n' >"$fixture/default/notes.txt"
truncate -s 41000 "$fixture/default/notes.txt"
mkdir "$fixture/default/projects"
export XDG_CONFIG_HOME="$fixture/xdg-config" XDG_DATA_HOME="$fixture/xdg-data"
export XDG_CACHE_HOME="$fixture/xdg-cache" XDG_STATE_HOME="$fixture/xdg-state"
export XDG_RUNTIME_DIR="$fixture/xdg-runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export GDK_BACKEND=x11
export G_DEBUG=fatal-criticals
export NO_AT_BRIDGE=1

run_app() {
    local home=$1
    shift
    # A shared session bus can forward activation to an already-running FileScope.
    # xvfb-run merges the command's stderr into stdout unless we restore it.
    HOME="$home" dbus-run-session -- xvfb-run -a -e /dev/stderr \
        -s '-screen 0 1600x1050x24' bash -c 'exec "$@" 2>&3' bash "$app" "$@" 3>&2
}

fail() {
    printf 'e2e [%s] %s\n' "$1" "$2" >&2
    cat "$fixture/$1.log" >&2
    exit 1
}

if output=$(run_app "$fixture/default" --smoke 2>"$fixture/smoke.log"); then
    [[ "$output" == "FileScope" ]] ||
        fail smoke "expected FileScope on stdout, got $(printf '%q' "$output")"
else
    fail smoke "app launcher exited with status $?"
fi
printf '%s\n' "$output"

capture() {
    local home=$1
    local stage=$2
    local preview=${3:-}
    local output_path="$fixture/main-$stage.png"
    FILESCOPE_UI_PREVIEW="$preview" run_app "$home" --screenshot "$output_path" \
        >"$fixture/$stage.log" 2>&1 || fail "$stage" "app launcher exited with status $?"
    [[ -s "$output_path" ]] || fail "$stage" "app produced no screenshot"
    mv -- "$output_path" "$screenshots/"
}

capture "$fixture/default" default
capture "$fixture/empty" empty
mkdir -m 000 "$fixture/locked"
error_home="$fixture/locked"
if [[ $(id -u) == 0 ]]; then
    error_home="$fixture/missing"
fi
capture "$error_home" error

G_MESSAGES_DEBUG=all capture "$fixture/default" chart chart &
chart_pid=$!
for ((attempt = 0; attempt < 100; ++attempt)); do
    if grep -q 'FileScope watch active' "$fixture/chart.log"; then
        break
    fi
    if ! kill -0 "$chart_pid" 2>/dev/null; then
        break
    fi
    sleep 0.02
done
if ! grep -q 'FileScope watch active' "$fixture/chart.log"; then
    wait "$chart_pid" || exit 1
    fail chart "watch never became active before fixture changes"
fi
dd if=/dev/zero of="$fixture/default/notes.txt" bs=1200 count=1 oflag=append conv=notrunc status=none
sleep 0.15
truncate -s 1200 "$fixture/default/notes.txt"
sleep 0.15
mv "$fixture/default/notes.txt" "$fixture/default/renamed.txt"
touch "$fixture/default/transient.txt"
rm "$fixture/default/transient.txt"
wait "$chart_pid" || exit 1
grep 'FileScope event-to-model latency:' "$fixture/chart.log" ||
    fail chart "fixture changes did not reach the model"
awk '/FileScope event-to-model latency:/ { if ($(NF-1) >= 2000000000) exit 1 }' "$fixture/chart.log" ||
    fail chart "event-to-model latency reached 2 seconds"
if cmp -s "$screenshots/main-default.png" "$screenshots/main-chart.png"; then
    fail chart "default and chart screenshots are identical"
fi
