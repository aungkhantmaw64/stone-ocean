# 09 — Watchdog, reset reason & factory reset

**Status:** todo
**Depends on:** 02, 04, 05
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/node_health`

## Goal
Make the node recover from hangs, record why it reset, and give the developer a way to wipe it back to factory state (no config, not provisioned).

## Context
- Runs in both normal and safe mode.
- Factory reset erases the config (spec 04) and, once spec 10 exists, the mesh state, through a hook that spec 10 registers.
- Extends the `status` CLI command (spec 05).

## Hardware details
- **BOOT button:** GPIO0 on the ESP32-DevKitC, active low with an on-board pull-up. GPIO0 is a strapping pin. Holding it during reset enters download mode, so only presses after boot count.
- **Watchdog:** uses the ESP-IDF Task Watchdog (TWDT). Reset reason comes from `esp_reset_reason()`.

## Scope
- In scope:
  - TWDT enabled with a 10 s timeout. The devices task (06), the console task and the main loop subscribe to it.
  - On boot, log the reset reason as text. Increment a crash counter in NVS (namespace `health`) when the reason is panic, task WDT, interrupt WDT or brownout. Reset the counter with a CLI command.
  - Factory reset triggers: CLI command `factory-reset` (asks for `factory-reset confirm`) and holding the BOOT button for ≥ 5 s after boot.
  - A factory-reset hook registry, so other modules (mesh) can register erase callbacks.
  - Status LED feedback is **not** required. Log lines only.
- Out of scope: boot-loop protection (future), remote health reporting via the mesh Health Server fault codes (future).

## Interface
```c
typedef void (*factory_reset_hook_t)(void);
void health_init(void);                       /* call early in boot */
int  health_register_reset_hook(factory_reset_hook_t hook);
void health_factory_reset(void);              /* runs hooks, erases config, reboots; does not return */
int  health_wdt_subscribe_current_task(void);
void health_wdt_feed(void);
const char *health_reset_reason_str(void);
uint32_t    health_crash_count(void);
void        health_crash_count_clear(void);
```
CLI additions: `factory-reset [confirm]`, `crash-clear`, and the `status` fields `reset_reason=`, `crash_count=`.

## Behavior & constraints
- Factory reset order: log `FACTORY RESET`, run registered hooks, then `config_store_erase()`, erase the `health` namespace, then `esp_restart()`.
- BOOT long-press detection uses a GPIO edge + timer through the HAL, not polling in a busy loop. Releasing before 5 s cancels it.
- The crash counter write happens once per boot, before the drivers start.

## Verification
- **Simulation/host:** Unity tests for reset-reason → string/counter logic (with an injected reason), long-press detection with the mock GPIO and time (4.9 s → no reset, 5 s → reset, bounce ignored), and hook ordering.
- **QEMU:**
  - `factory-reset confirm` erases the config and reboots into safe mode.
  - Add a hidden debug command `debug hang`, compiled only in debug builds, that blocks a subscribed task. The node resets via TWDT, and the next boot logs `reset_reason=task_wdt crash_count=1`.
- **On hardware:**
  - Same as QEMU.
  - Hold the BOOT button for 5 s after boot: the node factory-resets.
  - A short press does nothing.

## Acceptance criteria
- [ ] TWDT resets a hung subscribed task within ~10 s, and the next boot logs the reason and increments the crash counter.
- [ ] `status` shows the reset reason and crash count. `crash-clear` resets the count.
- [ ] `factory-reset confirm` and a 5 s BOOT press both erase the config, run hooks and reboot into safe mode.
- [ ] Long-press logic is host-tested with virtual time.
- [ ] `debug hang` is absent from release builds.

## Open questions
- None.
