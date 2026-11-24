#!/usr/bin/env bash
set -euo pipefail

if [[ ! -d src/core ]]; then
    echo "architecture: PASS (src/core does not exist at bootstrap stage)"
    exit 0
fi

if grep -RInE '^[[:space:]]*#include[[:space:]]+[<"]([^>"]*/)?ui/' src/core; then
    echo "architecture: core must not include ui" >&2
    exit 1
fi

echo "architecture: PASS"
