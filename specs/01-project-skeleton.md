# 01 — Project skeleton

**Status:** in-progress
**Depends on:** none
**Target:** ESP32-DevKitC (ESP32) · ESP-IDF v5.4.4 / FreeRTOS · idf.py, CMake, Python 3

## Goal
Create the empty but buildable project: the ESP-IDF firmware, a host unit-test build, a QEMU run path and CI. Every later chunk drops its code into this structure.

## Context
The project is a config-driven BLE mesh node (see `specs/README.md`). Three build flavours are needed from day one:
- **Hardware build:** full firmware, mesh enabled, flashed to a DevKitC over USB.
- **QEMU build:** same firmware with mesh disabled, because Espressif's QEMU does not emulate Bluetooth.
- **Host test build:** plain CMake + Unity, compiled with the PC's gcc. Tests pure-logic components against a mock HAL.

## Hardware details
ESP32-DevKitC with on-board USB-UART bridge. Flashing and logging go through `idf.py -p <port> flash monitor`. There is no JTAG. The modules are WROVER with 16 MB flash and 8 MB PSRAM (confirmed by the developer). The classic ESP32 maps 4 MB of PSRAM into the address space; the upper 4 MB is reachable only through the himem API.

## Scope
- In scope:
  - The `firmware/` ESP-IDF project with `main/`. Each component under `firmware/components/` is created by the spec that implements it, not stubbed in advance.
  - A custom partition table with a factory app partition large enough for Bluedroid + BLE Mesh (2 MB to start; the rest of the 16 MB flash stays unallocated), plus `nvs` and `phy_init`.
  - `sdkconfig.defaults`: PSRAM enabled (`CONFIG_SPIRAM=y`), Bluetooth enabled (BLE only, Bluedroid), `CONFIG_BLE_MESH=y`, and a project Kconfig option `NODE_MESH_ENABLE` (default `y`).
  - `sdkconfig.qemu`: `NODE_MESH_ENABLE=n`, Bluetooth disabled.
  - Helper scripts: `scripts/build.sh`, `scripts/qemu.sh` (wraps `idf.py qemu monitor` using the QEMU sdkconfig), `scripts/host_test.sh`.
  - `firmware/test/host/` CMake project fetching or vendoring Unity, with one passing sample test.
  - CI workflow (GitHub Actions or equivalent) that runs host tests and the hardware build using the `espressif/idf` Docker image.
  - `main` prints a boot banner: firmware version (from `git describe`), IDF version, build flavour.
- Out of scope: any feature logic (specs 02–13).

## Interface
- Kconfig: `CONFIG_NODE_MESH_ENABLE` (bool).
- `firmware/main/app_main.c`: `void app_main(void)` logs the banner and calls `boot_sequence_run()` (a stub for now, filled in by later specs).
- Scripts take the serial port from the `PORT` env var where relevant.

## Behavior & constraints
- Pin ESP-IDF v5.4.4 in README/CI. Mesh APIs differ between versions.
- Host test build must not include any ESP-IDF headers.
- Warnings as errors (`-Werror`) for project components.

## Verification
- **Simulation/host:** `scripts/host_test.sh` runs the sample Unity test and it passes. `scripts/qemu.sh` boots the QEMU build and the boot banner appears.
- **On hardware:** `idf.py -p $PORT flash monitor` shows the banner with flavour `hw`.

## Acceptance criteria
- [x] `idf.py build` succeeds with the default (hardware) config, with BLE Mesh compiled in.
- [x] QEMU build boots in Espressif QEMU and prints the banner with flavour `qemu`.
- [x] Host test build compiles with no ESP-IDF dependency and the sample test passes.
- [ ] CI runs host tests + hardware build on every push and fails on any error.
- [ ] Partition table fits the hardware build with ≥ 15% app-partition headroom (check with `idf.py size`).

Still to verify:
- CI (AC 4): the steps pass in `espressif/idf:v5.4.4` locally, but no GitHub remote exists yet, so the workflow has not run on GitHub.
- Headroom (AC 5): 91% free now, but nothing links the mesh code until spec 10. Re-check then.
- Hardware: banner with flavour `hw` on a DevKitC, the 16 MB flash size (`esptool.py flash_id`), and PSRAM detected at boot.

## Open questions
- None. Resolved: flash is 16 MB; modules are WROVER with 8 MB PSRAM; CI runs on GitHub Actions.
