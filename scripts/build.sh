#!/usr/bin/env bash
# Usage: scripts/build.sh [hw|qemu] [idf.py actions...]
#   Builds the firmware in the given flavour (default: hw) and runs the
#   idf.py actions (default: build). PORT selects the serial port.
#   Example: PORT=/dev/ttyUSB0 scripts/build.sh hw flash monitor
set -euo pipefail

firmware_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../firmware" && pwd)"

flavour="hw"
if [[ $# -gt 0 && ( "$1" == "hw" || "$1" == "qemu" ) ]]; then
    flavour="$1"
    shift
fi
actions=("$@")
[[ ${#actions[@]} -eq 0 ]] && actions=(build)

idf_args=(-C "$firmware_dir")
if [[ "$flavour" == "qemu" ]]; then
    build_dir="$firmware_dir/build-qemu"
    idf_args+=(-B "$build_dir"
               -D SDKCONFIG="$build_dir/sdkconfig"
               -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu")
fi
[[ -n "${PORT:-}" ]] && idf_args+=(-p "$PORT")

exec idf.py "${idf_args[@]}" "${actions[@]}"
