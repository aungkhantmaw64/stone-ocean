# 06 — Driver framework

**Status:** todo
**Depends on:** 02, 03
**Target:** ESP32 · ESP-IDF v5.x / FreeRTOS · `firmware/components/drivers`

## Goal
Provide a generic device layer that instantiates drivers from `node_config_t`, runs their sampling on a schedule, and turns readings and commands into events. This decouples device drivers from the mesh layer.

## Context
- At boot in normal mode, `devices_start(node_config_active())` creates one device instance per config entry.
- Concrete drivers: PIR and LED (07), SHTC3 (08).
- The mesh model mapping (11) subscribes to device events and calls `device_set_onoff` for actuators.
- Report-throttling logic (delta / min / max intervals) lives here, so it is shared and host-testable.

## Hardware details
N/A. Drivers access hardware only through the HAL (spec 02).

## Scope
- In scope:
  - Driver vtable, registry by `node_config_dev_type_t`, and device instances.
  - Init order: I2C buses first, then devices in config order.
  - A single "devices" FreeRTOS task with an event queue. Sampling timers and ISR signals post to the queue, and the task calls driver methods.
  - Reporting policy: emit a `DEV_EVT_READING` when the value changed by at least the configured delta and `min_interval_ms` has passed, or when `max_interval_ms` has passed since the last report.
  - Event subscription API (one or more listeners).
  - A `devices` CLI command listing devices, state and last reading.
- Out of scope: concrete drivers (07, 08), mesh (11).

## Interface
```c
typedef enum { DEV_EVT_READING, DEV_EVT_MOTION, DEV_EVT_ONOFF_CHANGED, DEV_EVT_FAULT } dev_evt_type_t;

typedef struct {
    float temp_c; float rh_pct;     /* SHTC3 */
    bool  motion;                   /* PIR */
    bool  on;                       /* LED */
} dev_value_t;

typedef struct { int dev_index; dev_evt_type_t type; dev_value_t value; uint32_t ts_ms; } dev_evt_t;
typedef void (*dev_listener_t)(const dev_evt_t *evt, void *ctx);   /* called in devices task */

typedef struct dev_driver {
    node_config_dev_type_t type;
    int  (*init)(int dev_index, const node_config_dev_t *cfg, void **state);
    int  (*sample)(void *state, dev_value_t *out);   /* NULL for actuators/event-only devices */
    int  (*set_onoff)(void *state, bool on);         /* NULL for sensors */
    void (*deinit)(void *state);
} dev_driver_t;

int  devices_register_driver(const dev_driver_t *drv);
int  devices_start(const node_config_t *cfg);   /* safe to call once */
int  devices_add_listener(dev_listener_t cb, void *ctx);
int  device_count(void);
const node_config_dev_t *device_cfg(int dev_index);
int  device_get_value(int dev_index, dev_value_t *out);
int  device_set_onoff(int dev_index, bool on);  /* thread-safe; posts to devices task */
void devices_post_from_isr(int dev_index, dev_evt_type_t type, const dev_value_t *v); /* for event drivers */
```
`device_index` equals the device's position in `config.devices[]`. The mesh layer relies on this.

## Behavior & constraints
- If a device fails `init` (for example, the SHTC3 doesn't answer), log it, mark that device `FAULT`, emit `DEV_EVT_FAULT` and continue with the other devices. Retry init every 30 s.
- If a sample fails 3 times in a row, the device is marked `FAULT` and an event is emitted. A successful sample clears the fault.
- Devices task stack ≤ 4 KB and priority below the BLE host tasks. All state is statically sized from `NODE_CONFIG_MAX_DEVICES`.
- Listener callbacks must not block. Document this.

## Verification
- **Simulation/host:** run the framework against fake drivers and the mock HAL/time. Unity tests cover:
  - devices start in config order
  - reporting policy (delta suppression, min interval, max-interval heartbeat)
  - fault after 3 failures, then recovery
  - init retry
  - `device_set_onoff` routing
  - multiple listeners
  
  The task/queue is abstracted so tests can drive the loop synchronously.
- **On hardware:** covered by specs 07 and 08 via the `devices` CLI command.

## Acceptance criteria
- [ ] Drivers register by type, and `devices_start` builds instances from config in order.
- [ ] Reporting policy behaves as specified (host tests with virtual time).
- [ ] Device faults are isolated, reported and retried without affecting other devices.
- [ ] `device_set_onoff` is safe to call from any task.
- [ ] `devices` CLI command shows index, id, type, state (ok/fault) and last value.

## Open questions
- None.
