# 07 — GPIO drivers: PIR & LED

**Status:** in-progress
**Depends on:** 06
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/node_dev/node_dev_pir.c`, `node_dev_led.c`

## Goal
Implement the PIR motion-input and LED output drivers as `node_dev_driver_t` plugins (spec 06), so a config entry of type `pir` or `led` produces a working device.

## Context
- PIR: event-driven. GPIO edge ISR → `node_dev_post_from_isr` → `on_isr` returns `debounce_ms` → the framework samples after the delay → `NODE_DEV_SIG_MOTION` published with `motion=true/false` when it changes.
- LED: actuator. `node_dev_set_onoff` → `set_onoff` → GPIO write, then the framework publishes `NODE_DEV_SIG_ONOFF_CHANGED`.
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
node_dev_err_t node_dev_pir_register(void);   /* registers the PIR driver (NODE_CONFIG_DEV_PIR, sample_sig MOTION) */
node_dev_err_t node_dev_led_register(void);   /* registers the LED driver (NODE_CONFIG_DEV_LED, sample_sig ONOFF_CHANGED) */
```

## Behavior & constraints
- The ISR does only `node_dev_post_from_isr`. Debounce happens in the devices AO: `on_isr` returns `debounce_ms`, every new edge restarts that delay, and the delayed sample reports the level once it has been stable for `debounce_ms`.
- A PIR emits an event only when the debounced level changes. Repeated edges within the debounce window produce at most one event.
- A PIR's initial state is published once after init (the framework samples every device right after init).
- LED `set_onoff` to the current state is a no-op, and the framework publishes no event because the state did not change.

## Verification
- **Simulation/host:** Unity tests with the mock HAL cover:
  - PIR debounce (glitch shorter than `debounce_ms` ignored, stable change → one event)
  - active-low polarity
  - initial state event
  - LED default state, polarity, no-op on same state, and the event on change
- **On hardware:**
  - Wire the PIR and LED per the example config and write it with the flashing tool (spec 05).
  - Wave a hand: the log shows `motion=1` and then `motion=0` after the hold time.
  - Write configs with `default_on` true and false, and with `active_high` true and false, and check the LED's state after each boot. Switching the LED on and off at runtime is verified over the mesh in specs 11 and 12.

## Acceptance criteria
- [x] PIR and LED drivers register and instantiate from config.
- [x] PIR debounce and polarity behave as specified (host tests).
- [x] The PIR ISR does no work beyond posting an event.
- [x] LED honours `default_on` and `active_high`, and emits events only on change (host tests).
- [ ] On hardware, motion events are logged and the LED's boot state follows `default_on` and `active_high`.

Host tests: `test_node_dev_pir` (14 tests) and `test_node_dev_led` (12 tests) run the real drivers on the framework and mock HAL, and pass under ASan, LSan and UBSan (commit 21adf40). Mutation checks confirmed the tests catch a missing debounce, ignored PIR or LED polarity, a same-state switch that rewrites the pin, and a failed switch recorded as done.

The PIR ISR (AC 3) is a single `node_dev_post_from_isr` call. The host tests check that an edge publishes and logs nothing until the devices AO runs, and that it queues exactly one event. An edge dropped because the devices AO's queue is full is not reported; the PIR keeps working from the next edge.

A QEMU boot with `configs/example-shtc3-pir-led.json` (commit c3d4301) logged `pir1` and `led1` as ok, with `motion=0` and `on=0` published once after start.

On the custom WROVER board with `configs/board-shtc3-pir.json` (PIR on GPIO 35, spec 08's hardware check), motion starts and stops were logged as `motion=1` and `motion=0`. Not yet verified on hardware: the LED half of AC 5, as the LED's pin is not confirmed.

## Open questions
- Exact PIR module and its output voltage. Confirm 3.3 V-compatible output before wiring.
- Final GPIO numbers. 27 (PIR) and 25 (LED) are placeholders.
