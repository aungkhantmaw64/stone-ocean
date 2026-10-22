# 05 — Serial CLI & PC upload tool

**Status:** todo
**Depends on:** 04
**Target:** ESP32 · ESP-IDF v5.x (`esp_console`, UART0) · `firmware/components/node_cli`, `tools/config_upload.py`

## Goal
Let a developer upload, inspect and erase the node config from a PC over USB serial, and query node status. This is the v1 config delivery path.

## Context
- Runs in both normal and safe mode. It is the only way to recover a node with a bad config in v1.
- Uses `node_config_parse` (03) to validate before `config_store_save` (04).
- Factory reset and status commands are extended by spec 09. Mesh status is extended by spec 10.

## Hardware details
UART0 via the DevKitC's USB-UART bridge, 115200 baud (IDF default). ESP-IDF log output shares this UART, so the PC tool must tolerate interleaved log lines.

## Scope
- In scope:
  - `esp_console` REPL with the commands below.
  - A chunked, CRC-checked upload protocol that fits within the console line-length limit.
  - Python tool `tools/config_upload.py` (pyserial), with `upload`, `show`, `erase` and `status` subcommands. `upload` validates against the JSON Schema locally first.
- Out of scope: binary protocols, and the mesh config transfer (future).

## Interface
Device commands. Each reply ends with a line starting `OK` or `ERR <code> <message>`, so the tool can find it among log lines:

| Command | Effect |
|---|---|
| `cfg begin <total_len>` | Start upload. Rejects if `total_len` > 4096. Discards any upload in progress. |
| `cfg chunk <seq> <base64>` | Append chunk `seq` (starting at 0, consecutive). Base64 payload ≤ 128 chars. |
| `cfg commit <crc32_hex>` | Verify length and CRC32, parse and validate (spec 03), then save (spec 04). On validation failure: `ERR VALIDATION <path>: <msg>`. |
| `cfg abort` | Discard upload in progress. |
| `cfg show` | Print the stored raw JSON between `---BEGIN CFG---` and `---END CFG---` lines. |
| `cfg erase` | Erase the stored config. Takes effect on reboot. |
| `status` | Print `mode=normal|safe`, the safe reason, firmware version and uptime. Specs 09 and 10 append fields. |
| `reboot` | Restart. |

PC tool:
```
python tools/config_upload.py --port /dev/ttyUSB0 upload configs/example-shtc3-pir-led.json [--reboot]
python tools/config_upload.py --port /dev/ttyUSB0 show | erase | status
```
Exit code 0 on success. Non-zero exits print the device's `ERR` line.

## Behavior & constraints
- The upload buffer is static (4096 B). A sequence gap gives `ERR SEQ` and aborts the upload.
- An upload left idle for 30 s is aborted automatically.
- Commands are processed in the console task. Never block it for more than 100 ms, apart from the flash write on commit.
- The tool retries a chunk once on timeout. Overall upload of a 4 KB config should take less than 5 s.

## Verification
- **Simulation/host:**
  - Unity tests for the upload state machine (begin/chunk/commit/abort, sequence gaps, CRC mismatch, oversize, timeout) with the console I/O layer separated out.
  - pytest for the Python tool's chunking/CRC, using a fake serial port.
- **QEMU:** the tool connects via QEMU's serial port, mapped to a TCP socket. pyserial supports `socket://localhost:<port>`. Upload the example config, reboot, and confirm normal mode via `status`.
- **On hardware:** same against `/dev/ttyUSB0`. `cfg show` returns exactly the uploaded bytes.

## Acceptance criteria
- [ ] All commands in the table work and end with `OK`/`ERR` lines.
- [ ] An invalid config is rejected at commit with the JSON path in the error, and the stored config is unchanged.
- [ ] `config_upload.py upload` works against QEMU (`socket://`) and real hardware.
- [ ] The tool tolerates interleaved ESP-IDF log lines.
- [ ] Host tests cover the upload state machine and the tool's chunking.

## Open questions
- None.
