# 09 — Watchdog, reset reason & factory reset

**Status:** in-progress
**Depends on:** 02, 04, 05
**Target:** custom ESP32 WROVER board · ESP-IDF v5.x · `firmware/components/node_health`

## Goal
Make the node recover from hangs, record why it reset, and give the developer a way to wipe it back to factory state (no config, not provisioned).

## Context
- Runs in both normal and safe mode.
- Factory reset erases the config (spec 04) and, once spec 10 exists, the mesh state, through a hook that spec 10 registers.
- The reset button and the debug hang are handled by a small **health AO** (spec 14), started by `node_health_init()`. The reset button's edge ISR only posts to it.
- Reports through the boot log; the node has no command interface. Adds a `factory-reset` subcommand to the PC flashing tool (spec 05).

## Hardware details
- **Reset button:** GPIO34 (`NODE_HAL_BOARD_RESET_BUTTON_GPIO`, spec 16), active low. GPIO34 is input-only and has no internal pull resistor, so the board pulls it up externally; a press pulls it low. Only presses after boot count.
- GPIO0 is the status LED (spec 16), not a button.
- **Watchdog:** uses the ESP-IDF Task Watchdog (TWDT), configured in `sdkconfig.defaults` (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=10`, `CONFIG_ESP_TASK_WDT_PANIC=y`). Reset reason comes from `esp_reset_reason()`.

## Scope
- In scope:
  - TWDT enabled with a 10 s timeout that panics and resets the node. Every AO thread subscribes to it: the FreeRTOS port of `node_ao` (spec 14) adds each AO's thread to the TWDT when it starts and feeds it on every turn of the AO loop, which waits at most `NODE_AO_MAX_WAIT_MS` (1 s). An AO whose dispatch hangs therefore trips the TWDT. `app_main` returns after the boot sequence, so there is no main loop to subscribe.
  - Initialise NVS at boot (`nvs_flash_init`). If it reports no free pages or a newer format version, log at ERROR, erase the `nvs` partition and retry. This wipes the mesh state too, but happens only after a partition layout or ESP-IDF version change.
  - On boot, log the reset reason as text. Increment a crash counter in NVS (namespace `health`) when the reason is panic, task WDT, interrupt WDT or brownout. Log `reset_reason=<reason> crash_count=<n>` once per boot. Factory reset clears the counter.
  - Factory reset triggers:
    - holding the reset button for ≥ 5 s after boot;
    - from the PC, `tools/config_flash.py factory-reset`, which erases the `config` and `nvs` partitions with esptool. This clears the config, the mesh state and the crash counter without running the hooks, and the node then boots unprovisioned in safe mode. `--flash-image` works for QEMU, as in spec 05.
  - A factory-reset hook registry, so other modules (mesh) can register erase callbacks.
  - Debug Kconfig option `NODE_HEALTH_DEBUG_HANG` (default off, never set in `sdkconfig.defaults` or `sdkconfig.qemu`): 15 s after boot, the health AO's dispatch never returns, so its thread stops feeding the watchdog, to test the TWDT.
  - Status LED feedback is **not** required. Log lines only.
- Out of scope: boot-loop protection (future), remote health reporting via the mesh Health Server fault codes (future).

## Interface
```c
typedef enum {
  NODE_HEALTH_OK = 0,
  NODE_HEALTH_ERR_ARG,    /* NULL hook */
  NODE_HEALTH_ERR_FULL,   /* hook table full */
  NODE_HEALTH_ERR_STATE,  /* not initialised, or already initialised */
  NODE_HEALTH_ERR_IO,     /* NVS failure */
} node_health_err_t;

typedef void (*node_health_reset_hook_t)(void);

void              node_health_init(void);     /* call first in the boot sequence; starts the health AO */
node_health_err_t node_health_register_reset_hook(node_health_reset_hook_t hook);
void              node_health_factory_reset(void); /* runs hooks, erases config, reboots; does not return */
const char       *node_health_reset_reason_str(void);
uint32_t          node_health_crash_count(void);
node_health_err_t node_health_crash_count_clear(void);
```
Boot log line: `reset_reason=<reason> crash_count=<n>`. PC tool addition: `config_flash.py factory-reset`.

## Behavior & constraints
- Factory reset order: log `FACTORY RESET`, run registered hooks, then `config_store_erase()`, erase the `health` namespace, then `esp_restart()`.
- Reset-button long-press detection uses a GPIO edge + timer through the HAL, not polling in a busy loop. Releasing before 5 s cancels it.
- The crash counter write happens once per boot, before the drivers start.

## Verification
- **Simulation/host:** Unity tests for reset-reason → string/counter logic (with an injected reason), long-press detection with the mock GPIO and time (4.9 s → no reset, 5 s → reset, bounce ignored), and hook ordering.
- **QEMU:**
  - `config_flash.py --flash-image ... factory-reset`, then boot with `--reuse`: the node boots in safe mode with `crash_count=0`.
  - A build with `NODE_HEALTH_DEBUG_HANG` resets via TWDT, and the next boot logs `reset_reason=task_wdt crash_count=1`.
- **On hardware:**
  - Same as QEMU.
  - Hold the reset button for 5 s after boot: the node factory-resets.
  - A short press does nothing.

## Acceptance criteria
- [ ] TWDT resets a hung subscribed task within ~10 s, and the next boot logs the reason and increments the crash counter.
- [ ] The boot log shows the reset reason and crash count. Factory reset clears the count.
- [ ] A 5 s reset-button press erases the config, runs the hooks and reboots into safe mode.
- [ ] `config_flash.py factory-reset` erases the `config` and `nvs` partitions, and the node boots unprovisioned in safe mode.
- [ ] Long-press logic is host-tested with virtual time.
- [ ] `NODE_HEALTH_DEBUG_HANG` is off in the hardware and QEMU builds.
