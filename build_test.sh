#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
binary="$repo_dir/build/rpi_motor_control_test"

mkdir -p "$(dirname -- "$binary")"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  "$repo_dir/dsp/f28379d/cpu1/tests/rpi_motor_control_test.cpp" \
  -o "$binary"
"$binary" --self-test
echo "Built $binary"
