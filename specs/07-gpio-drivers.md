# 07 — GPIO drivers: PIR & LED

**Status:** todo
**Depends on:** 06
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/drivers/pir.c`, `led.c`

## Goal
Implement the PIR motion-input and LED output drivers as `dev_driver_t` plugins, so a config entry of type `pir` or `led` produces a working device.

## Context
- PIR: event-driven. GPIO edge ISR → `devices_post_from_isr` → debounced `DEV_EVT_MOTION` with `motion=true/false`.
- LED: actuator. `device_set_onoff` → `set_onoff` → GPIO write, then `DEV_EVT_ONOFF_CHANGED`.
- The mesh mapping (11) turns these into Sensor Server (Motion Sensed) and Generic OnOff Server.

## Hardware details
- **PIR:** the exact module is not yet chosen. Common modules such as the HC-SR501 output a digital level that stays high for a module-set hold time after motion. Confirm the module's supply voltage and output level against its datasheet. The ESP32 GPIO is 3.3 V and **not 5 V tolerant**. Polarity comes from `active_high`, and pull from `pull`.
- **LED:** external LED + series resistor on the configured GPIO. Polarity comes from `active_high`. Size the resistor so pin current stays well within the ESP32 GPIO drive limits (datasheet).

## Scope
- In scope:
  - PIR: edge interrupt on any edge, debounce (`debounce_ms`), emit motion start/stop events, `sample` returns the current debounced level.
  - LED: drive to `default_on` at init, `set_onoff`, `sample` returns current state.
- Out of scope: PWM dimming (future), PIR sensitivity/hold settings (set on the module itself).

## Interface
```c
extern const dev_driver_t pir_driver;   /* type DEV_PIR */
extern const dev_driver_t led_driver;   /* type DEV_LED */
void gpio_drivers_register(void);        /* registers both with the framework */
```

## Behavior & constraints
- The ISR does only `devices_post_from_isr`. Debounce happens in the devices task: a level must stay stable for `debounce_ms` before it counts.
- A PIR emits an event only when the debounced level changes. Repeated edges within the debounce window produce at most one event.
- A PIR at init reads the current level and emits the initial state once.
- LED `set_onoff` to the current state is a no-op and emits no event.

## Verification
- **Simulation/host:** Unity tests with the mock HAL cover:
  - PIR debounce (glitch shorter than `debounce_ms` ignored, stable change → one event)
  - active-low polarity
  - initial state event
  - LED default state, polarity, no-op on same state, and the event on change
- **On hardware:**
  - Wire the PIR and LED per the example config and upload it (spec 05).
  - Wave a hand: a motion event is logged, and `devices` shows `motion=1` and then `motion=0` after the hold time.
  - Add a temporary CLI command `dev set <id> on|off` to toggle the LED, and verify the LED physically.

## Acceptance criteria
- [ ] PIR and LED drivers register and instantiate from config.
- [ ] PIR debounce and polarity behave as specified (host tests).
- [ ] The PIR ISR does no work beyond posting an event.
- [ ] LED honours `default_on` and `active_high`, and emits events only on change (host tests).
- [ ] On hardware, motion events are logged and the LED toggles via `dev set`.

## Open questions
- Exact PIR module and its output voltage. Confirm 3.3 V-compatible output before wiring.
- Final GPIO numbers. 27 (PIR) and 25 (LED) are placeholders.
