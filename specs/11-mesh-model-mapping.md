# 11 — Mesh model mapping

**Status:** todo
**Depends on:** 06, 07, 08, 10
**Target:** ESP32-DevKitC · ESP-IDF v5.x ESP-BLE-MESH · `firmware/components/node_mesh/mesh_models.c`

## Goal
Expose each configured device on the mesh through standard SIG models, so a gateway can read sensors and switch actuators with no custom protocol.

## Context
- Composition and elements come from spec 10. This spec provides the `mesh_models_builder_t`.
- Device data and commands go through the driver framework (06): this module is a `dev_listener_t`, and calls `device_set_onoff` / `device_get_value`.
- The gateway script (12) is the counterpart client.

## Hardware details
N/A.

## Scope
- In scope:
  - Model per device type, on that device's element:

    | Device type | Models | Properties / state |
    |---|---|---|
    | `shtc3` | Sensor Server (0x1100) + Sensor Setup Server (0x1101) | Present Ambient Temperature, Present Ambient Relative Humidity |
    | `pir` | Sensor Server (0x1100) + Sensor Setup Server (0x1101) | Motion Sensed |
    | `led` | Generic OnOff Server (0x1000) | OnOff |

    **Property IDs must be taken from the Bluetooth Mesh Device Properties specification.** Expected values to confirm: Present Ambient Temperature `0x004F`, Present Ambient Relative Humidity `0x0076`, Motion Sensed `0x0042`. Encode values using the characteristic formats defined there (temperature in 0.5 °C steps, humidity in 0.01 % steps, motion as a percentage 0–100 where 0 = no motion). Verify each format before implementing.
  - Sensor Server handles Sensor Descriptor Get and Sensor Get (all properties, or a single property).
  - Generic OnOff Server handles Get / Set / Set Unacknowledged, and replies with Status.
  - Publication: when the gateway has configured a publish address for a model, publish Sensor Status / Generic OnOff Status:
    - on every `DEV_EVT_READING`, which the framework already throttles with min/max/delta (spec 06)
    - immediately on `DEV_EVT_MOTION`
    - on `DEV_EVT_ONOFF_CHANGED`, from any source
  - Fault mapping: a device in `FAULT` answers Sensor Get with an empty/unknown value per the spec's "value unknown" rules. It also logs.
- Out of scope: Sensor Cadence/Settings beyond what the Setup Server needs to compile, vendor models, Generic OnOff transition time / delay (accepted but applied instantly in v1).

## Interface
```c
void mesh_models_init(void);   /* registers builder with mesh_node and listener with devices */
/* Internal helpers, exposed for host tests: */
size_t mesh_encode_temp(float temp_c, uint8_t *out);      /* returns bytes written */
size_t mesh_encode_rh(float rh_pct, uint8_t *out);
size_t mesh_encode_motion(bool motion, uint8_t *out);
size_t mesh_build_sensor_status(int dev_index, uint16_t prop_id /* 0 = all */, uint8_t *out, size_t cap);
```

## Behavior & constraints
- Message handlers run in the BLE mesh task. They must not block. Reading the latest value is a cached read via `device_get_value`, and actuating posts via `device_set_onoff`.
- A Generic OnOff Set to the current state still replies with Status (acknowledged variant) but does not republish.
- Encoding is little-endian, per the mesh spec. Out-of-range values are clamped to the characteristic's range.
- Only enabled when the node is provisioned and composition matches (spec 10).

## Verification
- **Simulation/host:** Unity tests cover:
  - value encoders (known answers incl. negative temperature, 0/100 % RH, rounding)
  - Sensor Status marshalling, both the single-property and all-properties forms (Marshalled Property ID format A/B per spec)
  - the event → publish decision table
  - OnOff Set → `device_set_onoff` routing (mocked)
- **On hardware**, with the gateway script (spec 12):
  - Sensor Get on the SHTC3 element returns values matching `devices`.
  - Motion publishes reach the gateway within 1 s.
  - Generic OnOff Set toggles the LED, and a Status comes back.

## Acceptance criteria
- [ ] Each device type exposes exactly the models in the table on its own element.
- [ ] Property IDs and value encodings match the Mesh Device Properties spec (checked in review, with host known-answer tests).
- [ ] Sensor Descriptor Get and Sensor Get (single and all) work against the gateway.
- [ ] Generic OnOff Get/Set/Set Unack work, and the LED changes physically.
- [ ] Publications are emitted per the event rules when a publish address is configured, and none when it isn't.
- [ ] Faulted devices report unknown values instead of stale ones.

## Open questions
- Confirm all property IDs and characteristic encodings against the current Mesh Device Properties specification.
- Motion Sensed as a percentage vs. a boolean-like 0/100. Proposed: 0 or 100 in v1.
