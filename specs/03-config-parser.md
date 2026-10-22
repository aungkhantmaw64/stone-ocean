# 03 — Config schema & parser

**Status:** done
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
- Reject GPIO 1 and 3 (UART0 TX/RX, used by the serial console and CLI, spec 05).
- Reject GPIO 34–39 for outputs and for internal pull resistors.
- Reject pins above 39.
- Warn (don't reject) on strapping pins 0, 2, 5, 12, 15.
- I2C `port` must be 0 or 1 (the ESP32 has two I2C controllers).
- I2C `sda` and `scl` are outputs (open drain), so the output rules apply to them.

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
#define NODE_CONFIG_MAX_DEVICES   8
#define NODE_CONFIG_MAX_I2C_BUSES 2
#define NODE_CONFIG_ID_LEN        16
#define NODE_CONFIG_NAME_LEN      32
#define NODE_CONFIG_MAX_RAW_LEN   4096
#define NODE_CONFIG_ERR_PATH_LEN  48
#define NODE_CONFIG_ERR_MSG_LEN   80

typedef enum { NODE_CONFIG_DEV_SHTC3, NODE_CONFIG_DEV_PIR, NODE_CONFIG_DEV_LED } node_config_dev_type_t;

typedef struct {
    char id[NODE_CONFIG_ID_LEN];
    int port;
    int sda;
    int scl;
    uint32_t freq_hz;
} node_config_i2c_bus_t;

typedef struct {
    uint32_t min_interval_ms, max_interval_ms;
    float delta_temp_c, delta_rh_pct;
} node_config_report_t;

typedef struct { int bus_index; uint32_t sample_interval_ms; node_config_report_t report; } node_config_shtc3_t;
typedef struct { int gpio; bool active_high; uint32_t debounce_ms; node_hal_gpio_pull_t pull; } node_config_pir_t;
typedef struct { int gpio; bool active_high; bool default_on; } node_config_led_t;

typedef struct {
    char id[NODE_CONFIG_ID_LEN];
    node_config_dev_type_t type;
    union {
        node_config_shtc3_t shtc3;
        node_config_pir_t pir;
        node_config_led_t led;
    };
} node_config_dev_t;

typedef struct {
    int version;
    char node_name[NODE_CONFIG_NAME_LEN];
    int n_i2c; node_config_i2c_bus_t i2c[NODE_CONFIG_MAX_I2C_BUSES];
    int n_dev; node_config_dev_t dev[NODE_CONFIG_MAX_DEVICES];
} node_config_t;

typedef enum {
    NODE_CONFIG_OK = 0,
    NODE_CONFIG_ERR_ARG,          /* NULL pointer or bad argument from the caller */
    NODE_CONFIG_ERR_TOO_LARGE,    /* input longer than NODE_CONFIG_MAX_RAW_LEN */
    NODE_CONFIG_ERR_SYNTAX,       /* malformed JSON */
    NODE_CONFIG_ERR_MISSING,      /* required key absent */
    NODE_CONFIG_ERR_TYPE,         /* value has the wrong JSON type */
    NODE_CONFIG_ERR_RANGE,        /* value out of range, or too long */
    NODE_CONFIG_ERR_VERSION,      /* unsupported "version" */
    NODE_CONFIG_ERR_DUPLICATE,    /* id or GPIO used twice, or I2C port used twice */
    NODE_CONFIG_ERR_PIN,          /* pin rule violated */
    NODE_CONFIG_ERR_DEVICE_TYPE,  /* unknown device "type" */
    NODE_CONFIG_ERR_BUS_REF,      /* device "bus" names no bus */
    NODE_CONFIG_ERR_TOO_MANY,     /* more buses or devices than the maximum */
    NODE_CONFIG_ERR_NO_MEMORY,    /* cJSON could not allocate */
} node_config_err_code_t;

typedef struct { node_config_err_code_t code; char path[NODE_CONFIG_ERR_PATH_LEN]; char msg[NODE_CONFIG_ERR_MSG_LEN]; } node_config_err_t;  /* path e.g. "devices[1].gpio" */

node_config_err_code_t node_config_parse(const char *json, size_t len, node_config_t *out, node_config_err_t *err);
uint32_t node_config_composition_hash(const node_config_t *cfg); /* hash of device types+order, used by spec 10 */
```

## Behavior & constraints
- Validation rules:
  - `version` must be 1.
  - IDs are unique, non-empty and shorter than `NODE_CONFIG_ID_LEN`.
  - Device `bus` references an existing bus.
  - No two devices or buses claim the same GPIO.
  - Pin rules as above.
  - No two buses use the same I2C `port`.
  - `freq_hz` must be between 10 kHz and 400 kHz.
  - Intervals: `sample_interval_ms` ≥ 1000, and `min_interval_ms` ≤ `max_interval_ms`.
- Unknown keys produce a warning (logged) but do not fail. Unknown `type` is an error.
- Warnings go through an internal log function: `ESP_LOGW` on the target, and a host mock with the same file name that records them, so tests can check them.
- Defaults for omitted optional fields:
  - `debounce_ms`: 50
  - `pull`: none
  - `default_on`: false
  - `report.*`: min 5000, max 60000, delta_temp_c 0.5, delta_rh_pct 2.0
- Input over `NODE_CONFIG_MAX_RAW_LEN` is rejected before parsing.
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
- [x] JSON Schema and example config exist, and the example validates against the schema.
- [x] `node_config_parse` accepts the example and fills every field, including defaults.
- [x] Every validation rule above has a host test producing the correct error path.
- [x] Parser builds and runs in the host test build (cJSON vendored or fetched for host).
- [x] No memory leaks in host tests (run under AddressSanitizer/LeakSanitizer).
- [x] `node_config_composition_hash` behaves as specified.

Verified on the host: `scripts/host_test.sh` runs 80 node_config tests (`test_node_config_parse`, `test_node_config_validate`, `test_node_config_composition`) under ASan, LSan and UBSan, all passing. The example config is embedded in the tests at build time and validated by the `config-schema` CI job, which has not yet run on GitHub (see spec 01).

## Open questions
- Should the SHTC3 entry allow overriding the I2C address? The SHTC3 has a fixed address, so the address is omitted from v1.
