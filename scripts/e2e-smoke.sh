#!/usr/bin/env bash
set -euo pipefail

fixture=$(mktemp -d)
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
export GDK_BACKEND=x11
export G_DEBUG=fatal-criticals
output=$(HOME="$fixture/default" xvfb-run -a "$1" --smoke)
printf '%s\n' "$output"
[[ "$output" == "FileScope" ]]

capture() {
    local home=$1
    local output_path=$2
    local preview=${3:-}
    HOME="$home" XDG_CONFIG_HOME="$fixture/xdg-config" XDG_DATA_HOME="$fixture/xdg-data" \
        FILESCOPE_UI_PREVIEW="$preview" xvfb-run -a -s '-screen 0 1600x1050x24' \
        "$app" --screenshot "$output_path"
    test -s "$output_path"
}

app=$1
capture "$fixture/default" "$screenshots/main-default.png"
capture "$fixture/empty" "$screenshots/main-empty.png"
mkdir -m 000 "$fixture/locked"
error_home="$fixture/locked"
if [[ $(id -u) == 0 ]]; then
    error_home="$fixture/missing"
fi
capture "$error_home" "$screenshots/main-error.png"

G_MESSAGES_DEBUG=all capture "$fixture/default" "$screenshots/main-chart.png" chart \
    >"$fixture/chart.log" 2>&1 &
chart_pid=$!
for ((attempt = 0; attempt < 100; ++attempt)); do
    if grep -q 'FileScope watch active' "$fixture/chart.log"; then
        break
    fi
    sleep 0.02
done
if ! grep -q 'FileScope watch active' "$fixture/chart.log"; then
    cat "$fixture/chart.log"
    wait "$chart_pid"
    exit 1
fi
dd if=/dev/zero of="$fixture/default/notes.txt" bs=1200 count=1 oflag=append conv=notrunc status=none
sleep 0.15
truncate -s 1200 "$fixture/default/notes.txt"
sleep 0.15
mv "$fixture/default/notes.txt" "$fixture/default/renamed.txt"
touch "$fixture/default/transient.txt"
rm "$fixture/default/transient.txt"
if ! wait "$chart_pid"; then
    cat "$fixture/chart.log"
    exit 1
fi
grep 'FileScope event-to-model latency:' "$fixture/chart.log"
awk '/FileScope event-to-model latency:/ { if ($(NF-1) >= 2000000000) exit 1 }' "$fixture/chart.log"
! cmp -s "$screenshots/main-default.png" "$screenshots/main-chart.png"
