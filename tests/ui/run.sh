#!/usr/bin/env bash
set -euo pipefail

fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
export GDK_BACKEND=x11 G_DEBUG=fatal-criticals NO_AT_BRIDGE=0 LC_ALL=C.UTF-8

run_suite() {
    local script=$1 app=$2 directory="$fixture/$1"
    mkdir -m 700 "$directory"
    export HOME="$directory/home" XDG_CONFIG_HOME="$directory/config" XDG_DATA_HOME="$directory/data"
    export XDG_CACHE_HOME="$directory/cache" XDG_STATE_HOME="$directory/state"
    export XDG_RUNTIME_DIR="$directory/runtime"
    mkdir -m 700 "$HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
        "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR"
    dbus-run-session -- xvfb-run -a -s '-screen 0 1600x1050x24' \
        python3 -B "$(dirname "$0")/$script.py" "$app" "$directory"
}

case "${2:-}" in
    tray) run_suite tray_interaction "$1" ;;
    history) run_suite history_interaction "$1" ;;
    *)
        run_suite interaction "$1"
        run_suite history_interaction "$1"
        run_suite tray_interaction "$1"
        ;;
esac
