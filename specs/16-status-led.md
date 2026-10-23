# 16 — Status LED

**Status:** in-progress
**Depends on:** 02, 03, 14
**Target:** custom ESP32 WROVER board · ESP-IDF v5.x · `firmware/components/node_status`, `node_hal/include/node_hal_board.h`, `node_config/node_config_validate.c`

## Goal
Give the node a status LED it can blink on its own, to show its state locally. The LED is part of the board, not of the config: it is always on GPIO0, it is never a configured device and it is never exposed to the mesh.

## Context
- The node runs on a custom board with a classic ESP32 WROVER module. The board has two fixed parts the firmware must know about without a config:
  - the **status LED** on GPIO0, active low;
  - the **reset button** on GPIO34, used for factory reset in spec 09.
- These pins live in one board header, `node_hal_board.h`. The firmware and the PC config checker (`tools/node_config_check`, which already includes `node_hal/include`) both read it, so a config can never claim them.
- Config LEDs (`"type": "led"`, specs 07 and 11) are separate. They stay steady on/off actuators in the devices AO, and the gateway switches them over the mesh with Generic OnOff. They do not blink.
- The status LED is owned by its own small active object, the **status AO** (spec 14), with its own time event. It is not part of the devices AO: it does not come from the config, it publishes nothing, and nothing subscribes to it. Other code switches it with commands posted to the status AO.
- The status AO starts before the config is loaded, so it works in both normal and safe mode (spec 04).
- Callers are later specs, for example safe mode, factory reset (09) and provisioning (10). Which state shows which pattern is decided there.

## Hardware details
- **Status LED:** LED and series resistor from 3.3 V to GPIO0. Driving GPIO0 low lights it.
- GPIO0 is a strapping pin: low at reset enters download mode. The active-low wiring never pulls GPIO0 low, and the pin is an input with a pull-up until the status AO configures it, so the LED is off from reset until then.
- Size the resistor so the pin's sink current stays well within the ESP32 GPIO drive limits (datasheet).
- **Reset button:** GPIO34, input-only, with an external pull-up on the board; a press pulls it low. It is listed here only so the config validator can reserve it; spec 09 uses it.

## Scope
- In scope:
  - `node_hal_board.h`: the board's fixed pins and their polarity.
  - The status AO in a new `node_status` component: steady on, steady off, and blinking with separate on and off times.
  - Starting the status AO from `boot_sequence_run()`, before `config_store_mode_boot()`.
  - The config validator and the JSON schema reject GPIO0 and GPIO34.
  - The HAL self-test's default button pin (`CONFIG_NODE_HAL_SELFTEST_BUTTON_GPIO`) moves from 0 to 34, as GPIO0 is now an output.
- Out of scope:
  - Which node states show which pattern (later specs).
  - Patterns beyond on and off times (double flashes, counted blinks), PWM fading.
  - Any change to the devices AO, config LEDs or the mesh mapping.

## Interface
```c
/* node_hal_board.h — header only; no ESP-IDF headers, so the PC checker can include it */
#define NODE_HAL_BOARD_STATUS_LED_GPIO        0
#define NODE_HAL_BOARD_STATUS_LED_ACTIVE_HIGH false
#define NODE_HAL_BOARD_RESET_BUTTON_GPIO      34

/* node_status.h */
#define NODE_STATUS_BLINK_MIN_MS 20   /* shortest on or off time, in ms */

typedef enum {
    NODE_STATUS_OK = 0,
    NODE_STATUS_ERR_ARG,    /* bad argument */
    NODE_STATUS_ERR_STATE,  /* not started, or already started */
    NODE_STATUS_ERR_FULL,   /* the status AO's queue is full; the command is dropped */
    NODE_STATUS_ERR_IO,     /* the LED's pin could not be set up */
} node_status_err_t;

node_status_err_t node_status_start(void);                              /* task context, once */
node_status_err_t node_status_led_set(bool on);                         /* any task; never blocks */
node_status_err_t node_status_led_blink(uint16_t on_ms, uint16_t off_ms); /* any task; never blocks */
```

## Behavior & constraints
- **Start:**
  - `node_status_start` starts the status AO. In its init event it configures the LED pin as an output and drives it off.
  - If the pin cannot be set up, the AO logs a warning and ignores every later command. The node boots normally.
  - Called a second time, it returns `NODE_STATUS_ERR_STATE`.
- **Commands:**
  - `node_status_led_set` and `node_status_led_blink` check their arguments and post to the status AO. They are safe from any task and never block. Before start they return `NODE_STATUS_ERR_STATE`; with a full queue, `NODE_STATUS_ERR_FULL`.
  - `node_status_led_blink` rejects an on or off time below `NODE_STATUS_BLINK_MIN_MS` with `NODE_STATUS_ERR_ARG`.
  - The latest command wins. A command replaces whatever the LED was doing, and a new blink command restarts the pattern with the new times.
- **Blinking:**
  - Blinking starts with the on phase. The status AO re-arms its one-shot time event at the end of each phase.
  - A phase may run late by the AO's dispatch latency (a few ms). Drift over many periods is acceptable for an indicator.
- **Polarity:** the AO drives the level given by `NODE_HAL_BOARD_STATUS_LED_ACTIVE_HIGH`, so callers only say on or off.
- **Errors:** a failed pin write is logged once when writes start failing and once when they recover, not on every phase.
- **Logging:** each command is logged at INFO when it changes the mode, for example `status led: blink 500/500` or `status led: on`. Individual toggles are not logged.
- **Isolation:** the status LED publishes no event and is not in `node_dev`'s device table, so the mesh mapping (spec 11) never sees it.
- **Config validation:**
  - A config that uses GPIO0 or GPIO34 for any pin (I2C, PIR, LED) is rejected with `NODE_CONFIG_ERR_PIN`, with messages `GPIO 0 is used by the status LED` and `GPIO 34 is used by the reset button`. The node then boots in safe mode, as for any invalid config (spec 04).
  - The JSON schema's `gpio_input` excludes 0 and 34, and its description says why.
- **Resources:** one AO with a 2 KB stack, a queue of 4 events and one time event. Its priority is below the devices AO. No dynamic allocation.

## Verification
- **Simulation/host:** Unity tests on the mock HAL and the host AO port with virtual time:
  - `test_node_status`: the pin is configured as an output and driven off at start; on and off with active-low levels; pin levels over several periods with unequal on and off times; a new blink restarts with the on phase; on or off ends blinking; argument checks, start twice, commands before start and a full queue; pin setup failure ignores later commands; write failures are logged once per failure streak; mode changes are logged and toggles are not.
  - `test_node_config_validate`: GPIO0 and GPIO34 are rejected for every pin field with the messages above. The strapping-pin test drops GPIO0 and the input-only test drops GPIO34.
  - `tools/tests`: the schema rejects GPIO0 and GPIO34.
- **QEMU:** a boot logs the status AO starting, and a config that uses GPIO0 boots in safe mode with the validation error.
- **On hardware:** call `node_status_led_blink(500, 500)` from a temporary call in `boot_sequence.c` (not committed). The LED blinks at about 1 Hz. Power-cycle the board several times: it always boots normally, never into download mode.

## Acceptance criteria
- [ ] The status AO drives GPIO0 active low: on, off and blinking with the requested times (host tests).
- [ ] The status commands are safe from any task, never block and reject bad arguments.
- [ ] The status AO starts before the config is loaded, and works in normal and safe mode.
- [ ] A config that uses GPIO0 or GPIO34 is rejected by the firmware, the PC checker and the schema.
- [ ] The status LED publishes no events and is invisible to the devices AO and the mesh.
- [ ] On hardware, the LED blinks visibly and the board always boots normally.

## Open questions
- Which node states show which pattern. To be decided by the specs that report them (safe mode, factory reset, provisioning).
