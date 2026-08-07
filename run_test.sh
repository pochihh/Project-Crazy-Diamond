#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
binary="$repo_dir/build/rpi_motor_control_test"

if [[ ! -x "$binary" ]]; then
  echo "Missing $binary; run ./build_test.sh first." >&2
  exit 1
fi

if [[ $# -eq 0 ]]; then
  set -- --sine
fi

exec sudo taskset -c 3 chrt -f 80 "$binary" --enable-motors "$@"
