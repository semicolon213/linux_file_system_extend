#!/usr/bin/env bash
set -euo pipefail

fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
export HOME="$fixture/home" XDG_CONFIG_HOME="$fixture/config" XDG_DATA_HOME="$fixture/data"
export XDG_CACHE_HOME="$fixture/cache" XDG_STATE_HOME="$fixture/state"
export XDG_RUNTIME_DIR="$fixture/runtime"
mkdir -m 700 "$HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
    "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR"
export GDK_BACKEND=x11 G_DEBUG=fatal-criticals NO_AT_BRIDGE=0 LC_ALL=C.UTF-8
dbus-run-session -- xvfb-run -a -s '-screen 0 1600x1050x24' \
    python3 "$(dirname "$0")/interaction.py" "$1" "$fixture"
