# 04 — Config storage

**Status:** in-progress
**Depends on:** 03
**Target:** ESP32 · ESP-IDF v5.x · LittleFS (`joltwallet/littlefs` 1.22.3) · `firmware/components/config_store`, `firmware/main`

## Goal
Persist the raw JSON config in flash and load it at boot. If the config is missing or invalid, the node enters **safe mode**: it logs the error and starts no drivers, but keeps the CLI running.

## Context
- The raw JSON, not the parsed struct, is stored so the format can evolve and the config can be read back exactly as uploaded.
- Writers: CLI upload (05). In the future, mesh config transfer.
- Reader: the boot sequence in `main`, which passes the parsed config to the driver framework (06) and mesh (10).
- Factory reset (09) erases it.

## Hardware details
Uses a dedicated LittleFS partition, `config` (64 KB, subtype `littlefs`), added to `partitions.csv` after `factory`. LittleFS works in 4 KB blocks, so 64 KB holds the ≤ 4 KB config (`NODE_CONFIG_MAX_RAW_LEN`) plus a temporary copy while saving, with plenty of spare blocks. The partition is mounted at `/config`. Config storage never touches NVS, where the ESP-BLE-MESH stack (10) and health (09) keep their state.

## Scope
- In scope:
  - Save, load and erase the raw config as one file, `/config/config.bin`: a 16-byte header followed by the raw JSON. The header holds four little-endian `uint32_t` fields: magic `0x4746434E` ("NCFG"), format version (`CONFIG_STORE_FORMAT_VERSION`), JSON length, and the CRC32 (IEEE) of the JSON.
  - Atomic save: write `/config/config.tmp`, close it, then rename it over `config.bin`. LittleFS renames atomically, so a power cut leaves either the old config or the new one. On load, a bad magic, an unknown format version, a length that does not match the file size, or a CRC mismatch means corrupt.
  - Mount at boot. If the partition does not mount (first boot, or never formatted), format it and mount again.
  - A boot sequence step that loads the config, parses it (spec 03) and sets the node mode to `CONFIG_STORE_MODE_NORMAL` or `CONFIG_STORE_MODE_SAFE`.
- Out of scope: last-known-good fallback, and waiting for a config over the mesh (both future). Initialising NVS (spec 09).

## Interface
```c
/* config_store.h (config_store.c) */
#define CONFIG_STORE_FORMAT_VERSION 1  /* format version written to the file header */

typedef enum {
    CONFIG_STORE_OK = 0,
    CONFIG_STORE_ERR_ARG,        /* NULL pointer or bad argument from the caller */
    CONFIG_STORE_ERR_NOT_FOUND,  /* no config stored */
    CONFIG_STORE_ERR_CORRUPT,    /* bad header or CRC mismatch */
    CONFIG_STORE_ERR_TOO_LARGE,  /* input or stored config does not fit */
    CONFIG_STORE_ERR_IO,         /* flash or filesystem failure */
} config_store_err_t;

config_store_err_t config_store_save(const char *json, size_t len);  /* caller has already validated */
config_store_err_t config_store_load(char *buf, size_t buf_len, size_t *out_len);
config_store_err_t config_store_erase(void);

/* config_store_mode.h (config_store_mode.c) */
typedef enum { CONFIG_STORE_MODE_NORMAL, CONFIG_STORE_MODE_SAFE } config_store_mode_t;
void                 config_store_mode_boot(void);            /* load + parse + decide; logs SAFE MODE once */
config_store_mode_t  config_store_mode_get(void);
const node_config_t *config_store_mode_active_config(void);   /* NULL in safe mode */
const char          *config_store_mode_safe_reason(void);     /* human-readable reason, or NULL */
```
Keep the filesystem access behind a small file interface, `config_store_fs.h` (`config_store_fs_init`, `_size`, `_read`, `_write_atomic`, `_remove`), so the host build can use an in-memory fake with the same file name.

- `config_store_save()` trusts the caller to have validated the JSON (spec 05 does). It checks only its arguments and the size limit.
- `config_store_erase()` removes the file. Erasing when nothing is stored succeeds.

## Behavior & constraints
- Boot log in safe mode, at ERROR level, exactly once: `SAFE MODE: <reason>`. Example reasons: `no config stored`, `stored config is corrupt`, `devices[1].gpio: pin 7 is reserved for flash`.
- Safe mode starts: CLI (05), health (09), and mesh core (10) if the node is already provisioned.
- Safe mode does not start: drivers (06–08) or device model mapping (11).
- A new config saved at runtime is applied on the next reboot. There is no hot reload in v1.
- Flash wear: saving happens only on explicit upload, so no special wear handling is needed.

## Verification
- **Simulation/host:** Unity tests with the in-memory `config_store_fs` fake cover:
  - save/load round trip
  - CRC mismatch, or a corrupted header → `CORRUPT`
  - missing → `NOT_FOUND`
  - oversize → `TOO_LARGE`
  - the boot decision logic (normal vs safe, with each reason)
- **QEMU:** boot with an empty `config` partition shows `SAFE MODE: no config stored`. Boot with a `config` partition image pre-loaded with the example config shows normal mode, and so does a reset after it. After spec 05: save via the CLI, reboot, and the log shows normal mode.
- **On hardware:** same as QEMU, and the config survives a power cycle.

## Acceptance criteria
- [ ] Save, load and erase work with CRC verification (host tests).
- [ ] Boot with no config, a corrupt config or an invalid config enters safe mode with the correct reason logged once.
- [ ] Boot with a valid config enters normal mode, and `config_store_mode_active_config()` returns the parsed struct.
- [ ] Config persists across reset in QEMU and on hardware.
- [ ] Config storage uses its own LittleFS partition and does not touch NVS (mesh keys).

## Open questions
- None.
