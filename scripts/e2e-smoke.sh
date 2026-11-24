#!/usr/bin/env bash
set -euo pipefail

output=$(xvfb-run -a "$1" --smoke)
printf '%s\n' "$output"
[[ "$output" == "FileScope" ]]
