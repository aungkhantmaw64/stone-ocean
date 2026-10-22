# 04 — Config storage

**Status:** todo
**Depends on:** 03
**Target:** ESP32 · ESP-IDF v5.x (NVS) · `firmware/components/config_store`, `firmware/main`

## Goal
Persist the raw JSON config in flash and load it at boot. If the config is missing or invalid, the node enters **safe mode**: it logs the error and starts no drivers, but keeps the CLI running.

## Context
- The raw JSON, not the parsed struct, is stored so the format can evolve and the config can be read back exactly as uploaded.
- Writers: CLI upload (05). In the future, mesh config transfer.
- Reader: the boot sequence in `main`, which passes the parsed config to the driver framework (06) and mesh (10).
- Factory reset (09) erases it.

## Hardware details
Uses the `nvs` partition from spec 01. NVS blobs can span pages, but keep the stored value ≤ 4 KB (`NODE_CFG_MAX_RAW_LEN`). The ESP-BLE-MESH stack also stores its state in NVS, so use a dedicated namespace.

## Scope
- In scope:
  - Save, load and erase the raw config under NVS namespace `nodecfg`. Keys: `raw` (blob), `crc` (u32, CRC32 of `raw`), `ver` (u32 schema version).
  - Atomic-enough save: validate before saving, write `raw`, then write `crc`, then commit. On load, a CRC mismatch means invalid.
  - A boot sequence step that loads the config, parses it (spec 03) and sets the node mode to `NODE_MODE_NORMAL` or `NODE_MODE_SAFE`.
- Out of scope: last-known-good fallback, and waiting for a config over the mesh (both future).

## Interface
```c
typedef enum { CFG_STORE_OK, CFG_STORE_NOT_FOUND, CFG_STORE_CORRUPT, CFG_STORE_TOO_LARGE, CFG_STORE_IO } cfg_store_err_t;

cfg_store_err_t config_store_save(const char *json, size_t len);  /* caller has already validated */
cfg_store_err_t config_store_load(char *buf, size_t buf_len, size_t *out_len);
cfg_store_err_t config_store_erase(void);

typedef enum { NODE_MODE_NORMAL, NODE_MODE_SAFE } node_mode_t;
node_mode_t        node_mode_get(void);
const node_config_t *node_config_active(void);  /* NULL in safe mode */
const char         *node_safe_reason(void);     /* human-readable reason, or NULL */
```
Keep the NVS access behind a small `kv_store` interface (get/set/erase blob and u32) so the host build can use an in-memory fake.

## Behavior & constraints
- Boot log in safe mode, at ERROR level, exactly once: `SAFE MODE: <reason>`. Example reasons: `no config stored`, `config CRC mismatch`, `devices[1].gpio: pin 7 is reserved for flash`.
- Safe mode starts: CLI (05), health (09), and mesh core (10) if the node is already provisioned.
- Safe mode does not start: drivers (06–08) or device model mapping (11).
- A new config saved at runtime is applied on the next reboot. There is no hot reload in v1.
- Flash wear: saving happens only on explicit upload, so no special wear handling is needed.

## Verification
- **Simulation/host:** Unity tests with the in-memory `kv_store` fake cover:
  - save/load round trip
  - CRC mismatch → `CORRUPT`
  - missing → `NOT_FOUND`
  - oversize → `TOO_LARGE`
  - the boot decision logic (normal vs safe, with each reason)
- **QEMU:** boot with empty NVS shows `SAFE MODE: no config stored`. After saving via the CLI (spec 05) and rebooting, the log shows normal mode.
- **On hardware:** same as QEMU, and the config survives a power cycle.

## Acceptance criteria
- [ ] Save, load and erase work with CRC verification (host tests).
- [ ] Boot with no config, a corrupt config or an invalid config enters safe mode with the correct reason logged once.
- [ ] Boot with a valid config enters normal mode, and `node_config_active()` returns the parsed struct.
- [ ] Config persists across reset in QEMU and on hardware.
- [ ] Config storage uses its own NVS namespace and does not touch mesh keys.

## Open questions
- None.
