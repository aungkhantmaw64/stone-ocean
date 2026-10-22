#!/usr/bin/env bash
# Usage: scripts/host_test.sh
#   Builds and runs the host unit tests (CMake + Unity, host gcc, no ESP-IDF).
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_dir/build/host"

cmake -S "$repo_dir/firmware/test/host" -B "$build_dir"
cmake --build "$build_dir"
cd "$build_dir" && ctest --output-on-failure
