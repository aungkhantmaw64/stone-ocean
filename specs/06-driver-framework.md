# 06 — Driver framework

**Status:** in-progress
**Depends on:** 02, 03, 14, 15
**Target:** ESP32 · ESP-IDF v5.x / FreeRTOS · `firmware/components/node_dev`

## Goal
Provide a generic device layer that instantiates drivers from `node_config_t`, runs their sampling on a schedule in one active object, and publishes readings, motion, on/off changes and faults as events. This decouples device drivers from the mesh layer.

## Context
- At boot in normal mode, `node_dev_start(config_store_mode_active_config())` creates one device instance per config entry.
- Concrete drivers: PIR and LED (07), SHTC3 (08). They register with `node_dev_register_driver()` before `node_dev_start()`.
- The devices AO owns every device. Following spec 15:
  - Data that changes is **published** (`NODE_DEV_SIG_*`). The mesh model mapping (11) subscribes and keeps its own copy of the latest values. Nobody reads the devices AO's state.
  - On/off is a **command**, posted directly to the devices AO by `node_dev_set_onoff()`.
- The device index is the device's position in `config.dev[]`. The mesh layer relies on this, and gets each device's config from the active config itself.
- Report-throttling logic (delta / min / max intervals) lives here, so it is shared and host-testable.

## Hardware details
N/A. Drivers access hardware only through the HAL (spec 02).

## Scope
- In scope:
  - Driver vtable, registry by `node_config_dev_type_t`, and device instances.
  - Init order: I2C buses first, then devices in config order.
  - A single devices AO (spec 14). Sampling, init retries, debounce delays and measurement waits all run from one time event, re-armed to the earliest pending deadline. ISRs only post to the AO.
  - Non-blocking driver calls: a driver's `sample` may return `NODE_DEV_PENDING` and ask to be called again later (for example while a measurement runs), and an `on_isr` hook lets an event-driven driver ask for a delayed sample (for example to debounce).
  - Reporting policy for `NODE_DEV_SIG_READING`: publish when the value changed by at least the configured delta and `min_interval_ms` has passed, or when `max_interval_ms` has passed since the last report.
  - Publishing device events with `node_ao_pubsub_publish()` (spec 15).
  - Device log lines: a table of devices (index, id, type, state) once at start, a line on every state change (ok ↔ fault), and every published reading, motion change and on/off change at INFO level.
- Out of scope: concrete drivers (07, 08), mesh (11), stopping or re-creating devices at runtime.

## Interface
```c
/* node_dev.h */
#define NODE_DEV_INIT_RETRY_MS     30000  /* time between init retries of a FAULT device */
#define NODE_DEV_MAX_SAMPLE_FAILS  3      /* failed samples in a row that make a device FAULT */

typedef enum {
    NODE_DEV_OK = 0, NODE_DEV_ERR_ARG, NODE_DEV_ERR_STATE, NODE_DEV_ERR_FULL,
    NODE_DEV_ERR_IO,       /* the device or its bus failed */
    NODE_DEV_PENDING,      /* sample started; call again after *again_ms */
} node_dev_err_t;

enum {  /* published signals, from node_ao_pub_sigs.h; subscribe with node_ao_pubsub_subscribe() */
    NODE_DEV_SIG_READING = NODE_AO_PUB_SIGS_NODE_DEV,  /* sensor reading, throttled by the report policy */
    NODE_DEV_SIG_MOTION,                               /* motion started or stopped */
    NODE_DEV_SIG_ONOFF_CHANGED,                        /* actuator switched on or off */
    NODE_DEV_SIG_FAULT,                                /* device entered (fault=true) or left (fault=false) FAULT */
};

typedef struct {
    float temp_c; float rh_pct;     /* SHTC3 */
    bool  motion;                   /* PIR */
    bool  on;                       /* LED */
} node_dev_value_t;

typedef struct {                    /* every published event; fits NODE_AO_MAX_EVT_SIZE */
    node_ao_evt_t    super;         /* one of NODE_DEV_SIG_* */
    uint8_t          dev_index;     /* position in config.dev[] */
    bool             fault;         /* NODE_DEV_SIG_FAULT only */
    node_dev_value_t value;
    uint32_t         ts_ms;         /* node_hal_time_ms() when published */
} node_dev_evt_t;

typedef struct {
    int                       dev_index;
    const node_config_dev_t  *cfg;
    node_hal_i2c_bus_t        bus;  /* the device's I2C bus; NULL for GPIO devices */
} node_dev_init_args_t;

typedef struct {                    /* filled in by a driver's init */
    uint32_t                    sample_interval_ms;  /* 0 = not sampled periodically */
    const node_config_report_t *report;              /* NODE_DEV_SIG_READING drivers only */
} node_dev_sched_t;

typedef struct node_dev_driver {
    node_config_dev_type_t type;
    node_ao_sig_t          sample_sig;  /* NODE_DEV_SIG_READING, _MOTION or _ONOFF_CHANGED */
    node_dev_err_t (*init)(const node_dev_init_args_t *args, void **state, node_dev_sched_t *sched);
    node_dev_err_t (*sample)(void *state, node_dev_value_t *out, uint32_t *again_ms); /* NULL for actuators */
    node_dev_err_t (*set_onoff)(void *state, bool on);                               /* NULL for sensors */
    uint32_t       (*on_isr)(void *state);  /* NULL if no ISR; returns ms until the framework samples */
} node_dev_driver_t;

node_dev_err_t node_dev_register_driver(const node_dev_driver_t *drv);  /* before node_dev_start() */
node_dev_err_t node_dev_start(const node_config_t *cfg);                /* once; cfg must stay valid */
node_dev_err_t node_dev_set_onoff(int dev_index, bool on);              /* any task; posts to the devices AO */
node_dev_err_t node_dev_post_from_isr(int dev_index);                   /* ISR context; for event drivers */
```
Driver methods run in the devices AO's thread and must not block: no delays and no waiting on queues or semaphores. A short HAL bus transfer is allowed, and so is a busy-wait of at most `NODE_HAL_TIME_BUSY_WAIT_MAX_US` (1 ms) with `node_hal_time_busy_wait_us()`, for device timings shorter than a tick.

## Behavior & constraints
- **Start:**
  - `node_dev_start` returns `NODE_DEV_ERR_ARG` for a NULL config and `NODE_DEV_ERR_STATE` when called a second time.
  - In the devices AO's init event: initialise the I2C buses in config order, then each device in config order, then log the device table.
  - A device whose type has no registered driver is logged, marked `FAULT`, published as a fault and never retried.
- **Init failures:** if a device fails `init` (for example, the SHTC3 doesn't answer), or its I2C bus failed to initialise, log it, mark that device `FAULT`, publish `NODE_DEV_SIG_FAULT` and continue with the other devices. Retry every `NODE_DEV_INIT_RETRY_MS`, re-initialising the bus first if it had failed.
- **Sampling:**
  - A device with a `sample` method is sampled once right after a successful init, then every `sample_interval_ms` if that is non-zero.
  - `NODE_DEV_PENDING` calls `sample` again after `again_ms`, and does not count as a failure.
  - `NODE_DEV_MAX_SAMPLE_FAILS` failed samples in a row mark the device `FAULT`, and it keeps being sampled on schedule. A successful sample clears the fault.
- **ISR events:**
  - `node_dev_post_from_isr` posts to the devices AO and never blocks.
  - The AO calls the driver's `on_isr`, then samples the device after the returned delay. A new ISR event before then restarts the delay.
- **Publishing:**
  - Samples of a `NODE_DEV_SIG_READING` driver follow the reporting policy:
    - The first successful sample is always published.
    - Deltas are measured against the last *published* value: a report is due when the temperature or the humidity changed by at least its delta and `min_interval_ms` has passed, or when `max_interval_ms` has passed.
  - Samples of a `NODE_DEV_SIG_MOTION` or `NODE_DEV_SIG_ONOFF_CHANGED` driver are published when `motion` or `on` differs from the last published value. The first sample after start is always published, so subscribers learn the initial state.
  - `NODE_DEV_SIG_FAULT` is published on every change between ok and `FAULT`, with `fault` set to the new state.
- **On/off:**
  - `node_dev_set_onoff` checks the index range and posts. It is safe from any task and never blocks.
  - In the AO, a device that is out of range, has no `set_onoff`, or is in `FAULT` is logged and ignored.
  - A successful `set_onoff` publishes `NODE_DEV_SIG_ONOFF_CHANGED` when `on` differs from the last published state. A failed one is logged.
- **Resources:**
  - Devices AO stack is 4 KB, and its priority is below the BLE host tasks.
  - All state is statically sized from `NODE_CONFIG_MAX_DEVICES`. No dynamic allocation.
  - The AO uses one time event.
- Subscribers receive events by value in their own thread. A full subscriber queue drops that subscriber's copy, and the devices AO logs a warning.

## Verification
- **Simulation/host:** run the framework against fake drivers, the mock HAL and the host AO port (spec 14), which drives the AO synchronously on virtual time. Unity tests cover:
  - devices start in config order
  - reporting policy (delta suppression, min interval, max-interval heartbeat)
  - fault after 3 failures, then recovery, with both fault events published
  - init retry
  - pending samples, and ISR-triggered delayed samples
  - `node_dev_set_onoff` routing
  - every subscriber receives the published events
- **On hardware:** covered by specs 07 and 08 via the device log lines.

## Acceptance criteria
- [x] Drivers register by type, and `node_dev_start` builds instances from config in order.
- [x] Reporting policy behaves as specified (host tests with virtual time).
- [x] Device faults are isolated, published and retried without affecting other devices.
- [x] `node_dev_set_onoff` is safe to call from any task.
- [x] Driver calls never block the devices AO: pending samples and ISR-triggered samples are scheduled with time events (host tests).
- [ ] The log shows the device table at start (index, id, type, state), every state change, and every published reading.

Host tests: `test_node_dev_report` (14 tests) and `test_node_dev` (42 tests) pass under ASan, LSan and UBSan (commit 064f253). Mutation checks confirmed the tests catch a drifting sample period, a deadline that fires late, a failure count that never resets, a second interrupt that does not restart the delay, and on/off published without a change.

`node_dev_set_onoff` (AC 4) only posts to the devices AO and never blocks, also with a full queue (host tests). Calls from several real tasks are exercised on the board through spec 11, from the mesh task.

Not yet verified on hardware: the log lines (AC 6). A QEMU boot with `configs/example-shtc3-pir-led.json` (commit 1bc0a90) logged the device table and every state change, and the host tests check that every published reading is logged. Real readings in the board log need the drivers of specs 07 and 08.

## Open questions
- None.
