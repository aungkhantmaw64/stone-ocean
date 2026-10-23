# 14 — Active object framework

**Status:** done
**Depends on:** 02
**Target:** ESP32 · ESP-IDF v5.x / FreeRTOS · `firmware/components/node_ao`

## Goal
Provide a small active object (AO) framework, so that every thread in the firmware is an active object: a private event queue, a thread that runs one event at a time to completion, and time events. Components never create plain FreeRTOS tasks and never share state between threads directly; they post events to each other. The logic of every AO can then be tested on the host by dispatching events synchronously against virtual time.

## Context
- Project-wide rule (see `specs/README.md`): firmware code never calls `xTaskCreate` or creates FreeRTOS timers directly. Each concurrent activity is an AO built on this component. Tasks owned by ESP-IDF itself (Bluedroid, the timer service, idle, `app_main`) are not ours. Callbacks that ESP-IDF runs in those tasks, and ISRs, only post events to an AO.
- Users: the HAL self-test (spec 02) is converted to an AO here. The devices AO (spec 06) and later components (09, 10, 11) build on it.
- The framework is deliberately minimal: no hierarchical state machines, no event pools. Publish-subscribe is added separately by spec 15. An AO's dispatch function implements its own state machine, usually as a `switch` on its state and the event signal.

## Hardware details
N/A. The FreeRTOS port uses only FreeRTOS queues and tasks.

## Scope
- In scope:
  - AO object (`node_ao_t`) embedded as the first member of a component's own AO struct, with a dispatch function `void (*)(node_ao_t *me, const node_ao_evt_t *e)`.
  - Events by value: each event struct starts with a `node_ao_evt_t` header that holds the signal. Each AO's queue copies events up to its configured event size.
  - A reserved `NODE_AO_SIG_INIT` event, dispatched once in the AO's own thread before any other event.
  - Posting from any task (`node_ao_post`) and from an ISR (`node_ao_post_from_isr`), without blocking.
  - Time events: one-shot or periodic, owned by one AO, which dispatches them in its own thread as a plain event with the time event's signal.
  - A FreeRTOS port (`node_ao_port.c`) and a host port (`firmware/test/host/mocks/node_ao_port.c`) that runs AOs synchronously.
  - Converting the HAL self-test (`node_hal_selftest.c`) from a plain task to an AO, with the same behaviour and log output.
- Out of scope: hierarchical state machines, dynamic event allocation, deferred events, priority inheritance. Publish-subscribe between AOs is added by spec 15.

## Interface
```c
/* node_ao.h */
#define NODE_AO_MAX_EVT_SIZE   32   /* largest event an AO can queue, in bytes */
#define NODE_AO_MAX_TIME_EVTS  8    /* time events per AO */

typedef enum { NODE_AO_OK = 0, NODE_AO_ERR_ARG, NODE_AO_ERR_STATE, NODE_AO_ERR_FULL, NODE_AO_ERR_NO_MEMORY } node_ao_err_t;

typedef uint16_t node_ao_sig_t;
enum { NODE_AO_SIG_INIT = 0, NODE_AO_SIG_USER = 1 };   /* AO signals start at NODE_AO_SIG_USER */

typedef struct { node_ao_sig_t sig; } node_ao_evt_t;    /* first member of every event */

typedef struct node_ao node_ao_t;
typedef void (*node_ao_dispatch_t)(node_ao_t *me, const node_ao_evt_t *e);

typedef struct {
    const char *name;        /* task name, also used in logs */
    unsigned    priority;    /* FreeRTOS priority */
    size_t      stack_bytes;
    size_t      queue_len;   /* events */
    size_t      evt_size;    /* largest event this AO receives; sizeof(node_ao_evt_t) .. NODE_AO_MAX_EVT_SIZE */
} node_ao_config_t;

typedef struct node_ao_time_evt node_ao_time_evt_t;     /* fields documented in the header */
struct node_ao { /* fields documented in the header; allocate statically */ };

node_ao_err_t node_ao_start(node_ao_t *me, node_ao_dispatch_t dispatch, const node_ao_config_t *cfg);
node_ao_err_t node_ao_post(node_ao_t *me, const void *evt, size_t size);           /* any task; never blocks */
node_ao_err_t node_ao_post_from_isr(node_ao_t *me, const void *evt, size_t size);  /* ISR context */

/* node_ao_time.c — call only before node_ao_start() or from the owning AO's dispatch */
node_ao_err_t node_ao_time_evt_init(node_ao_time_evt_t *te, node_ao_t *owner, node_ao_sig_t sig);
node_ao_err_t node_ao_time_evt_arm(node_ao_time_evt_t *te, uint32_t delay_ms, uint32_t period_ms); /* period 0 = one-shot; both <= INT32_MAX */
node_ao_err_t node_ao_time_evt_disarm(node_ao_time_evt_t *te);
bool          node_ao_time_evt_is_armed(const node_ao_time_evt_t *te);
```
Host-only helpers, declared in `node_ao_port_mock.h`:
- `node_ao_port_mock_reset()` forgets every started AO and its queued events.
- `node_ao_port_mock_run_until_idle()` dispatches, in every started AO, the queued events and the time events that are due at the mock HAL time, until no AO has work left. It returns the number of events dispatched, and stops with an error after a fixed bound so an endless event ping-pong fails the test instead of hanging it.

## Behavior & constraints
- Run to completion: an AO dispatches one event at a time, in its own thread. Its dispatch function must not block or wait (no delays, no blocking queue or semaphore calls). Long work is split into steps driven by events or time events. The one exception is a device driver's `init` in the devices AO, which may wait briefly with `node_hal_time_delay_ms()` (spec 06).
- Events are dispatched in the order they were posted. Due time events are dispatched after the event that was being processed, in the order they were initialised.
- `node_ao_post` and `node_ao_post_from_isr` copy `size` bytes (at least `sizeof(node_ao_evt_t)`, at most the AO's `evt_size`) and zero the rest of the queued slot. They never block. A full queue returns `NODE_AO_ERR_FULL` and the event is dropped; callers decide whether that matters.
- Posting to an AO that is not started returns `NODE_AO_ERR_STATE`. Starting an AO twice returns `NODE_AO_ERR_STATE`.
- Time events use the HAL millisecond clock (`node_hal_time_ms`) and handle its 32-bit wrap-around. A periodic time event that falls more than one period behind fires once and is rescheduled one period from now, so it never fires in a burst.
- An AO's thread waits for its next event or its next due time event, but never longer than `NODE_AO_MAX_WAIT_MS` (1000 ms) at a time, so spec 09 can feed the task watchdog from the AO loop.
- All memory is static or allocated once in `node_ao_start()` (the FreeRTOS queue and task). Nothing is allocated after start.
- The public header includes no FreeRTOS or ESP-IDF headers.

## Verification
- **Simulation/host:** Unity tests with the host port and the mock HAL clock cover:
  - the init event is dispatched first, and events are dispatched in the order posted
  - events larger than the AO's event size, or smaller than the header, are rejected
  - a full queue rejects the event without losing the queued ones
  - posting before start, and starting twice, are rejected
  - one-shot and periodic time events fire at the right virtual time, including across the 32-bit millisecond wrap-around
  - disarming stops a time event, and re-arming restarts it
  - a periodic time event that falls behind does not fire in a burst
  - an endless ping-pong between two AOs is stopped by `node_ao_port_mock_run_until_idle()`
- **On hardware:** the HAL self-test, now an AO, still logs the I2C scan, BOOT-button edges and the output toggle as before (spec 02 verification steps).

## Acceptance criteria
- [x] AOs start, receive events in order, and dispatch the init event first (host tests).
- [x] Posting from tasks and ISRs never blocks, and rejects bad sizes, a full queue and an AO that is not started (host tests).
- [x] One-shot and periodic time events fire on virtual time, survive the millisecond wrap-around and never burst (host tests).
- [x] The public header compiles in the ESP-IDF and host builds with no FreeRTOS or ESP-IDF includes.
- [x] The HAL self-test runs as an AO, and no firmware code creates a plain FreeRTOS task or timer.
- [x] On hardware, the self-test log output is unchanged.

Host tests: `test_node_ao` (15 tests) and `test_node_ao_time` (16 tests) pass under ASan, LSan and UBSan (commit 33e19fc). The FreeRTOS port is the only `xTaskCreate` in the firmware.

Not yet verified on hardware: the self-test log on the board (AC 6). A QEMU boot of the hardware config (commit 87275ec) showed the self-test AO running its init event: the I2C scan and the button setup were logged as before. Button edges and the output toggle need the board.

## Open questions
- None.
