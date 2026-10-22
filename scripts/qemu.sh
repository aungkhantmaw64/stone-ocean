#!/usr/bin/env bash
# Usage: scripts/qemu.sh [--config FILE | --reuse]
#   Builds the QEMU flavour and boots it in Espressif QEMU with the serial
#   monitor attached. Exit the monitor with Ctrl+].
#     (no option)    boot with an empty config partition, as on a new board
#     --config FILE  boot with FILE pre-loaded as the stored config
#     --reuse        boot the flash image of the previous run again, keeping
#                    everything the firmware wrote to it (persistence checks)
set -euo pipefail

scripts_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(dirname "$scripts_dir")"
build_dir="$repo_dir/firmware/build-qemu"
flash_image="$build_dir/qemu_flash_persistent.bin"

config=""
reuse=false
case "${1:-}" in
    "") ;;
    --config) config="$(realpath "${2:?--config needs a file}")" ;;
    --reuse) reuse=true ;;
    *) echo "usage: $0 [--config FILE | --reuse]" >&2; exit 2 ;;
esac

"$scripts_dir/build.sh" qemu build
if [[ "$reuse" == false ]]; then
    python "$repo_dir/tools/config_image.py" --build-dir "$build_dir" \
        --output "$flash_image" ${config:+--config "$config"}
elif [[ ! -f "$flash_image" ]]; then
    echo "no flash image from a previous run; run without --reuse first" >&2
    exit 1
fi

exec "$scripts_dir/build.sh" qemu qemu --flash-file "$flash_image" monitor
