# 05 — PC config flashing tool

**Status:** in-progress
**Depends on:** 04
**Target:** Linux PC · Python 3 · esptool · littlefs-python · CMake + host gcc · `tools/config_flash.py`, `tools/node_config_check/`

## Goal
Let a developer check, write, read back and erase the node config from a PC. The tool writes the config straight into the node's `config` flash partition over USB serial, so the firmware needs no code to receive a config. This is the v1 config delivery path.

## Context
- The node has no command interface over serial. UART0 carries only flashing (through the ROM bootloader) and the log. Runtime state is reported in the log.
- The tool writes the same `config.bin` file that config storage (04) reads at boot: the 16-byte header (magic, format version, length, CRC32) plus the raw JSON, inside a LittleFS image of the `config` partition. `tools/config_image.py` already packs this format for QEMU, and the two tools share that code.
- The firmware does not validate a config when it is written. It validates it at boot and enters safe mode if it is invalid (04). To catch errors before writing, the tool runs the firmware's own parser (03), built for the PC, after the JSON Schema check.
- Writing a config resets the node, because esptool enters the bootloader and resets the chip afterwards. A config only takes effect after a reboot anyway (04).
- Spec 09 adds a `factory-reset` subcommand to this tool.

## Hardware details
- UART0 via the DevKitC's USB-UART bridge. esptool puts the chip into the ROM bootloader and resets it through the DTR/RTS auto-reset circuit on the DevKitC, as `idf.py flash` already does.
- The tool finds the `config` partition by reading the partition table from the target at `0x8000`, so it always uses the layout that is actually on the node (`firmware/partitions.csv`).
- Writing and erasing touch only the `config` partition. The `nvs` partition, which holds the mesh state (10) and health data (09), is never written.

## Scope
- In scope:
  - `tools/config_flash.py` with the subcommands `check`, `write`, `read` and `erase`. The target is either a serial port (`--port`) or a QEMU flash image file (`--flash-image`).
  - `tools/node_config_check/`: a small CMake project that builds the firmware's `node_config` sources with cJSON (the same version as the host tests) into a PC program, `node_config_check`. It prints warnings and errors to stderr instead of the ESP-IDF log. `config_flash.py` builds it on first use.
  - Sharing the config packing and LittleFS image code between `config_flash.py` and `config_image.py`, and reading packed configs back (header and CRC check).
  - `tools/requirements.txt` with pinned `esptool`, `littlefs-python` (the version that `joltwallet/littlefs` pins for its own image builds), `jsonschema` and `pytest`. The CI `tool-tests` job installs it and builds `node_config_check`.
  - A "Writing a config to a node" section in the repository `README.md`.
- Out of scope: any firmware code that receives a config or commands over UART; the mesh config transfer (future); factory reset (09).

## Interface
```
python tools/config_flash.py check configs/example-shtc3-pir-led.json
python tools/config_flash.py --port /dev/ttyUSB0 write configs/example-shtc3-pir-led.json
python tools/config_flash.py --port /dev/ttyUSB0 read [--output FILE]
python tools/config_flash.py --port /dev/ttyUSB0 erase
python tools/config_flash.py --flash-image firmware/build-qemu/qemu_flash_persistent.bin write|read|erase ...
```

| Subcommand | Effect |
|---|---|
| `check FILE` | Check the size (1 to 4096 bytes), the JSON Schema and `node_config_check`. Needs no target. |
| `write FILE` | Run `check`, then build a LittleFS image holding `config.bin` and write it over the whole `config` partition. With `--port`, esptool verifies the write and resets the node. |
| `read` | Read the `config` partition, extract `config.bin`, check its header and CRC, and print the raw JSON exactly as stored (or write it to `--output`). |
| `erase` | Erase the `config` partition. The next boot finds no config and enters safe mode (04). |

Exit codes: `0` success; `1` the config is invalid (the message names the JSON path, as `<path>: <message>`); `2` usage error; `3` the target could not be read or written; `4` no config is stored, or the stored config is corrupt (`read` only).

`node_config_check FILE` exits `0` if `node_config_parse()` accepts the file and `1` otherwise, printing `<path>: <message>`. It prints each warning as `warning: <path>: <message>`.

## Behavior & constraints
- Nothing is written to the target unless `check` passes.
- `--flash-image` edits the image file in place. QEMU must not be running on that image at the time. The next `scripts/qemu.sh --reuse` boots with the change.
- The LittleFS image uses the same geometry as the firmware: 4096-byte blocks, the partition size from the partition table, and the firmware's `CONFIG_LITTLEFS_OBJ_NAME_LEN`.
- `read` must also read a `config` partition that the firmware itself wrote (`config_store_save()`, for the future mesh transfer).
- A `write` of a 4 KB config should take under 10 s on hardware, most of it esptool connecting and resetting.

## Verification
- **Simulation/host:**
  - pytest, using fake esptool and checker runners where a test does not need the real one:
    - partition table parsing (finding `config`, a missing `config` partition, a corrupt table);
    - packing and unpacking `config.bin`: round trip, a bad magic, version, length or CRC;
    - `write`, `read` and `erase` on a temporary flash image file, with real LittleFS images (`tmp_path`);
    - nothing is written when `check` fails, and the error names the JSON path;
    - the esptool commands issued for `--port` use the offset and size of the `config` partition only.
  - The host test build compiles `node_config_check` and a ctest runs it on the example config (exit 0) and on an invalid config (exit 1, with the path).
- **QEMU:** boot with an empty config (`scripts/qemu.sh`), stop QEMU, `write` the example config into the flash image, boot with `--reuse`: the log shows normal mode. `read` returns exactly the written bytes. `erase` then `--reuse`: `SAFE MODE: no config stored`.
- **On hardware:** the same against `/dev/ttyUSB0`. Mesh provisioning (once spec 10 exists) survives a `write` and an `erase`.

## Acceptance criteria
- [x] `check` rejects a config that breaks a JSON Schema rule or a firmware-only rule, and prints the JSON path.
- [ ] `write` stores the config without anything being written when it is invalid, and the node boots into normal mode with it (QEMU and hardware).
- [x] `read` returns exactly the bytes that were written, and reports a corrupt or missing config.
- [x] `erase` makes the next boot enter safe mode with `no config stored`.
- [x] `write` and `erase` touch only the `config` partition, never `nvs`.
- [x] pytest covers the partition table, packing, the image round trip and the refusal to write invalid configs. CI runs it, and builds and runs `node_config_check`.
- [x] The firmware contains no code that receives a config or commands over UART.

Verified on the PC and in QEMU: `pytest tools` (48 tests, including the real `node_config_check` when cmake is installed) and the two `node_config_check` ctests in `scripts/host_test.sh` pass. In QEMU, `write` → boot gives normal mode, `read` returns the exact bytes after the firmware has mounted the partition, `erase` → boot gives `SAFE MODE: no config stored`, and the `nvs` partition is unchanged. Still to do on hardware: `write`, `read` and `erase` with `--port` against a DevKitC (criterion 2, and the hardware half of 3 and 4). Mesh provisioning surviving a `write` can only be checked once spec 10 exists. The CI changes have not yet run on GitHub.

## Open questions
- None.
