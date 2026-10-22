# 03 — Config schema & parser

**Status:** todo
**Depends on:** 01
**Target:** ESP32 · ESP-IDF v5.x (cJSON component) · `firmware/components/node_config`, `configs/`

## Goal
Define the JSON config format and implement a parser/validator that turns raw JSON into a validated, fixed-size `node_config_t`, or a precise error. Everything the node does at runtime is driven by this struct.

## Context
- Raw JSON arrives via the serial CLI (spec 05), is stored by config storage (spec 04), and is parsed at boot. In the future it will also arrive over the mesh, so the parser must be transport-agnostic.
- The driver framework (spec 06) and mesh composition (specs 10, 11) consume `node_config_t`.
- The parser is pure logic and must build and test on the host.

## Hardware details
Pin validation rules for the classic ESP32. Confirm against the datasheet:
- Reject GPIO 6–11 (flash).
- Reject GPIO 16–17 (PSRAM on the WROVER module).
- Reject GPIO 24 and 28–31 (not present on the ESP32).
- Reject GPIO 34–39 for outputs and for internal pull resistors.
- Reject pins above 39.
- Warn (don't reject) on strapping pins 0, 2, 5, 12, 15.

## Scope
- In scope:
  - JSON schema `configs/node-config.schema.json` (JSON Schema draft 2020-12) for PC-side validation.
  - Example config `configs/example-shtc3-pir-led.json`.
  - C parser + validator using cJSON.
- Out of scope: storage (04), applying the config (06).

## Interface
Config format, version 1. The pins are placeholders:
```json
{
  "version": 1,
  "node_name": "lab-node-1",
  "i2c_buses": [
    { "id": "i2c0", "port": 0, "sda": 21, "scl": 22, "freq_hz": 100000 }
  ],
  "devices": [
    { "id": "env1", "type": "shtc3", "bus": "i2c0",
      "sample_interval_ms": 10000,
      "report": { "min_interval_ms": 5000, "max_interval_ms": 60000,
                  "delta_temp_c": 0.5, "delta_rh_pct": 2.0 } },
    { "id": "pir1", "type": "pir", "gpio": 27, "active_high": true,
      "debounce_ms": 50, "pull": "none" },
    { "id": "led1", "type": "led", "gpio": 25, "active_high": true, "default_on": false }
  ]
}
```

```c
#define NODE_CFG_MAX_DEVICES   8
#define NODE_CFG_MAX_I2C_BUSES 2
#define NODE_CFG_ID_LEN        16
#define NODE_CFG_MAX_RAW_LEN   4096

typedef enum { DEV_SHTC3, DEV_PIR, DEV_LED } node_dev_type_t;

typedef struct { char id[NODE_CFG_ID_LEN]; int port, sda, scl; uint32_t freq_hz; } node_i2c_bus_cfg_t;

typedef struct {
    uint32_t min_interval_ms, max_interval_ms;
    float delta_temp_c, delta_rh_pct;
} node_report_cfg_t;

typedef struct {
    char id[NODE_CFG_ID_LEN];
    node_dev_type_t type;
    union {
        struct { int bus_index; uint32_t sample_interval_ms; node_report_cfg_t report; } shtc3;
        struct { int gpio; bool active_high; uint32_t debounce_ms; hal_pull_t pull; } pir;
        struct { int gpio; bool active_high; bool default_on; } led;
    };
} node_dev_cfg_t;

typedef struct {
    int version;
    char node_name[32];
    int n_i2c; node_i2c_bus_cfg_t i2c[NODE_CFG_MAX_I2C_BUSES];
    int n_dev; node_dev_cfg_t dev[NODE_CFG_MAX_DEVICES];
} node_config_t;

typedef struct { int code; char path[48]; char msg[80]; } node_cfg_err_t;  /* path e.g. "devices[1].gpio" */

int  node_config_parse(const char *json, size_t len, node_config_t *out, node_cfg_err_t *err); /* 0 = ok */
uint32_t node_config_composition_hash(const node_config_t *cfg); /* hash of device types+order, used by spec 10 */
```

## Behavior & constraints
- Validation rules:
  - `version` must be 1.
  - IDs are unique, non-empty and shorter than `NODE_CFG_ID_LEN`.
  - Device `bus` references an existing bus.
  - No two devices or buses claim the same GPIO.
  - Pin rules as above.
  - `freq_hz` must be between 10 kHz and 400 kHz.
  - Intervals: `sample_interval_ms` ≥ 1000, and `min_interval_ms` ≤ `max_interval_ms`.
- Unknown keys produce a warning (logged) but do not fail. Unknown `type` is an error.
- Defaults for omitted optional fields:
  - `debounce_ms`: 50
  - `pull`: none
  - `default_on`: false
  - `report.*`: min 5000, max 60000, delta_temp_c 0.5, delta_rh_pct 2.0
- Input over `NODE_CFG_MAX_RAW_LEN` is rejected before parsing.
- cJSON allocations are freed before return. No allocation is kept in `out`.
- Error messages are human-readable and include the JSON path.

## Verification
- **Simulation/host:** Unity tests cover:
  - The example config parses.
  - Each validation rule has a failing case with the expected `path`.
  - Defaults are applied.
  - Oversize input is rejected.
  - Malformed JSON is rejected.
  - The composition hash changes when device order or type changes, and not when intervals change.
  
  The PC side runs `python -m jsonschema` (or `check-jsonschema`) against the example.
- **On hardware:** none needed. Covered via spec 05.

## Acceptance criteria
- [ ] JSON Schema and example config exist, and the example validates against the schema.
- [ ] `node_config_parse` accepts the example and fills every field, including defaults.
- [ ] Every validation rule above has a host test producing the correct error path.
- [ ] Parser builds and runs in the host test build (cJSON vendored or fetched for host).
- [ ] No memory leaks in host tests (run under AddressSanitizer/LeakSanitizer).
- [ ] `node_config_composition_hash` behaves as specified.

## Open questions
- Should the SHTC3 entry allow overriding the I2C address? The SHTC3 has a fixed address, so the address is omitted from v1.
