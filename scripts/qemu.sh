#!/usr/bin/env bash
# Usage: scripts/qemu.sh
#   Builds the QEMU flavour and boots it in Espressif QEMU with the serial
#   monitor attached. Exit the monitor with Ctrl+].
set -euo pipefail

exec "$(dirname "${BASH_SOURCE[0]}")/build.sh" qemu qemu monitor
