# 11 — Mesh model mapping (vendor model, protobuf)

**Status:** in-progress
**Depends on:** 06, 07, 08, 10
**Target:** custom ESP32 WROVER board · ESP-IDF v5.4.4 ESP-BLE-MESH · nanopb · `proto/`, `firmware/components/node_mesh/node_mesh_msg.c`, `node_mesh_models.c`, `node_mesh_models_port.c`

## Goal
Expose every configured device on the mesh through one Stone Ocean vendor model whose messages are Protocol Buffers. The gateway tool (spec 12) can then list a node's devices, read sensors, receive published readings and switch actuators. The `.proto` schema is the contract: our own tools and any third party talk to nodes through it.

## Context
- Bluetooth SIG models (Sensor Server, Generic OnOff Server) are deliberately not used. Nodes are managed only by our own tools. A single protobuf schema shared by the node, the gateway and any backend is easier to evolve than per-type SIG encodings.
- The schema lives in `proto/` at the repository root, so the firmware and the gateway tool (spec 12) generate code from the same file. The firmware uses **nanopb** (static allocation, no heap). The gateway uses the Python `protobuf` package.
- One vendor server model sits on the **primary element**. The device index travels in each message. The node's composition no longer depends on the config. Spec 10 drops its per-device elements and its composition-hash check as part of this spec (see **Changes to spec 10**).
- Device data and commands go through the driver framework (06). A **models AO** (spec 14) subscribes to the `NODE_DEV_SIG_*` published events (spec 15), keeps the latest value and fault state of each device in its own state, and switches actuators with `node_dev_set_onoff`. Each device's config comes from the active config, at the device index.
- The ESP-BLE-MESH model callback runs in Bluedroid's task. It only copies the request (sender, keys, opcode and the raw payload) into an event and posts it to the models AO. The AO decodes, builds every reply and publication, encodes it, and hands it to ESP-BLE-MESH, whose send and publish calls are safe from any task.
- Split into three files: `node_mesh_msg.c` (pure protobuf encode/decode of the messages, on the nanopb generated code), `node_mesh_models.c` (the models AO, free of ESP-IDF so host tests run it), and `node_mesh_models_port.c` (ESP-BLE-MESH glue: the vendor model, its callback, send and publish).
- The boot sequence starts the models AO in normal mode, before the devices AO (so it receives each device's first value) and before `node_mesh_start()` (so the vendor model is registered). In safe mode the vendor model is still registered and reports an empty device list.
- Later specs add messages to the same model: system-info heartbeat, node commands (reboot, factory reset, identify), config delivery and OTA. Their opcodes are reserved below.

## Hardware details
N/A.

## Scope
- In scope:
  - **Vendor model:** company ID `0x02E5` (Espressif) and model ID `0x0001`, "Stone Ocean Node Server", on element 0 with a publication context. The gateway tool's client uses model ID `0x0002`. The company ID is one constant, `NODE_MESH_MODELS_CID`. `TODO(confirm):` replace it with Stone Ocean's own Bluetooth SIG company ID once there is one.
  - **Opcodes:** 3-octet vendor opcodes `0xC0 | op` followed by the company ID (`ESP_BLE_MESH_MODEL_OP_3(op, CID)`). One opcode per message type:

    | op | Message | Direction | Reply |
    |---|---|---|---|
    | `0x01` | `DeviceListGet` | gateway → node | `DeviceList` |
    | `0x02` | `DeviceList` | node → gateway | — |
    | `0x03` | `DeviceGet` | gateway → node | one `DeviceStatus` per device asked for |
    | `0x04` | `DeviceStatus` | node → gateway (reply and publication) | — |
    | `0x05` | `OnOffSet` | gateway → node | `DeviceStatus` |
    | `0x06` | `OnOffSetUnack` (same payload as `OnOffSet`) | gateway → node | none |
    | `0x07`–`0x3F` | reserved for later specs (heartbeat, commands, config, OTA) | | |

  - **Schema** `proto/stone_ocean/mesh/v1/node.proto` (proto3, package `stoneocean.mesh.v1`), with a nanopb `.options` file that bounds every repeated and string field:

    ```proto
    enum DeviceType {
      DEVICE_TYPE_UNSPECIFIED = 0;
      DEVICE_TYPE_SHTC3 = 1;
      DEVICE_TYPE_PIR = 2;
      DEVICE_TYPE_LED = 3;
    }

    message DeviceInfo {
      DeviceType type = 1;
      string id = 2;                     // config id, at most 15 bytes
    }

    message DeviceListGet {}             // empty payload

    message DeviceList {
      repeated DeviceInfo devices = 1;   // at most 8; position = device index
    }

    message DeviceGet {
      optional uint32 device = 1;        // absent: every device
    }

    message DeviceStatus {
      uint32 device = 1;                 // device index
      optional sint32 temperature_dc = 2; // SHTC3, 0.1 °C
      optional uint32 humidity_dpct = 3;  // SHTC3, 0.1 %RH, 0 to 1000
      optional bool motion = 4;          // PIR
      optional bool on = 5;              // LED
      bool fault = 6;                    // true while the device is in FAULT
    }

    message OnOffSet {
      uint32 device = 1;
      bool on = 2;
    }
    ```

    Rules: value fields are present only for the device's own type. A device in FAULT, or one that has not reported a value yet, sends no value fields: absence means "unknown", never a stale value. A FAULT device also sets `fault`. Temperature is clamped to −45.0 to 130.0 °C (the range of the SHTC3's conversion formula) and humidity to 0–100 %, then both are rounded half away from zero to 0.1 units; a value that is not a finite number is left out. The clamp keeps every status within the size budget. The node sends no timestamp; it has no wall clock, so the gateway stamps arrival time.
  - **Size budget:** an unsegmented vendor message carries at most 8 bytes of payload after the 3-byte opcode. `DeviceGet`, `OnOffSet` and every `DeviceStatus` fit in it: the largest is an SHTC3 status, 8 bytes. `DeviceList` may be segmented (at most about 170 bytes for 8 devices). Requests whose payload is longer than 8 bytes are dropped and logged.
  - **Replies:**
    - `DeviceListGet` → `DeviceList` with every configured device, in config order. Empty in safe mode.
    - `DeviceGet` → one `DeviceStatus` for that device, or one per device, in index order, when `device` is absent.
    - `OnOffSet` → switch the LED with `node_dev_set_onoff` and reply at once with a `DeviceStatus` carrying the requested state, since v1 applies it instantly. If the LED is in FAULT, the devices AO ignores the set, so the reply carries the last known state (none) and `fault`.
    - `OnOffSetUnack` → the same action, no reply.
    - An `OnOffSet` to the current state still replies (acknowledged variant) but does not republish.
    - A request for a device index that does not exist, an `OnOffSet` to a device that is not an actuator, a payload that does not decode, or an unknown opcode is not answered and is logged.
  - **Publication:** when the gateway has configured a publish address for the vendor model, publish a `DeviceStatus`:
    - on every `NODE_DEV_SIG_READING`, which the framework already throttles with min/max/delta (spec 06)
    - immediately on `NODE_DEV_SIG_MOTION`
    - on `NODE_DEV_SIG_ONOFF_CHANGED`, from any source
    - on `NODE_DEV_SIG_FAULT`, entering or leaving FAULT

    With no publish address, nothing is published.
  - **Build:** nanopb at a pinned release. The runtime is built into the firmware and the host tests. The `.pb.c`/`.pb.h` files are generated at build time from `proto/` by the pinned `nanopb` generator (Python), in the hardware build, the host build and CI. Generated files are not committed.
- Out of scope: the heartbeat, node commands, config delivery and OTA messages (later specs, opcodes reserved); SIG Sensor and Generic models; authentication beyond the mesh application key.

## Changes to spec 10
Applied by this spec, because the composition no longer depends on the config:
- Composition: **element 0 only**, with Config Server, Health Server and the Stone Ocean Node Server. No per-device elements.
- The models builder becomes a hook that returns the vendor models of element 0: `node_mesh_set_vendor_models(esp_ble_mesh_model_t *models, uint8_t count)`, called before `node_mesh_start()`. `node_mesh_device_element()` and `node_mesh_models_builder_t` are removed.
- `node_mesh_start()` takes no config any more: the composition is the same in normal and safe mode.
- The composition hash, the `meshmeta` NVS namespace and the mismatch WARN are removed, and so is `node_config_composition_hash()` (spec 03), which has no other caller. Adding or removing a device no longer requires reprovisioning; the gateway rereads `DeviceList`. The README's open question on composition changes is closed.

## Interface
```c
/* include/node_mesh_models.h (no ESP-IDF headers) */
/* Start the models AO, subscribe it to the NODE_DEV_SIG_* events and register the vendor model with
 * node_mesh. Call before node_dev_start() and node_mesh_start(). cfg is NULL in safe mode; otherwise
 * it must stay valid. */
node_mesh_err_t node_mesh_models_start(const node_config_t *cfg);

/* node_mesh_msg.h: internal, pure, exposed for host tests. Encoders write the payload length to *len,
 * which may be 0: an empty protobuf message is a valid, empty payload. Every function returns
 * NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer, a bad argument, a buffer too small or a
 * payload that does not decode. */
#define NODE_MESH_MSG_MAX_UNSEGMENTED 8 /* payload bytes after a 3-byte vendor opcode */
#define NODE_MESH_MSG_DEVICE_LIST_MAX_LEN 168
#define NODE_MESH_MSG_OP_DEVICE_LIST_GET 0x01 /* ... to NODE_MESH_MSG_OP_ONOFF_SET_UNACK 0x06 */
node_mesh_err_t node_mesh_msg_encode_device_status(int dev_index, node_config_dev_type_t type,
                                                   const node_dev_value_t *value /* NULL = unknown */,
                                                   bool fault, uint8_t *out, size_t cap, size_t *len);
node_mesh_err_t node_mesh_msg_encode_device_list(const node_config_t *cfg /* NULL = empty */,
                                                 uint8_t *out, size_t cap, size_t *len);
node_mesh_err_t node_mesh_msg_decode_device_get(const uint8_t *in, size_t len,
                                                bool *all, int *dev_index);
node_mesh_err_t node_mesh_msg_decode_onoff_set(const uint8_t *in, size_t len,
                                               int *dev_index, bool *on);
```

## Behavior & constraints
- The mesh callback must not block: it only posts the request to the models AO. The AO answers from its own copy of the published device events and actuates by posting via `node_dev_set_onoff`.
- No heap: nanopb with static buffers and bounded fields only.
- Only active when the node is provisioned; before that, requests cannot arrive and publications are skipped.
- Every payload on the wire is the protobuf encoding of the message its opcode names; the opcode is the only framing, with no length or version prefix. The schema evolves by protobuf's rules: add fields, never renumber or reuse them.

## Verification
- **Simulation/host:** Unity tests cover:
  - message encoding with known-answer byte vectors (negative temperature, 0 % and 100 % RH, rounding, unknown and FAULT, the 8-byte budget), stored in `proto/testdata/` so spec 12's pytest checks the same vectors
  - decoding of `DeviceGet` and `OnOffSet`, including malformed and oversize payloads
  - the event → publish decision table
  - request → reply and `OnOffSet` → `node_dev_set_onoff` routing (mocked port)
- **On hardware**, with the gateway tool (spec 12):
  - `DeviceList` matches the config.
  - `DeviceGet` on the SHTC3 returns values matching the node's log.
  - Motion publishes reach the gateway within 1 s.
  - `OnOffSet` toggles the LED, and a `DeviceStatus` comes back.

## Acceptance criteria
- [ ] The node's composition is element 0 with Config Server, Health Server and the Stone Ocean Node Server (CID `0x02E5`, model `0x0001`), whatever the config.
- [ ] Messages are encoded and decoded per `proto/stone_ocean/mesh/v1/node.proto`, checked by host known-answer tests whose vectors live in `proto/testdata/`.
- [ ] Every `DeviceGet`, `OnOffSet` and `DeviceStatus` fits in one unsegmented message (≤ 8 payload bytes).
- [ ] `DeviceListGet` and `DeviceGet` (one device and all) work against the gateway.
- [ ] `OnOffSet` and `OnOffSetUnack` work, and the LED changes physically.
- [ ] Publications are emitted per the event rules when a publish address is configured, and none when it isn't.
- [ ] Faulted devices, and devices with no value yet, report no value fields instead of stale ones.
- [ ] Spec 10's changes are applied: element 0 only, no composition hash, and adding a device needs no reprovisioning.
- [ ] The hardware build still fits the factory app partition with ≥ 15 % headroom (`idf.py size`).

## Open questions
- Stone Ocean's own Bluetooth SIG company ID, to replace Espressif's `0x02E5` (`TODO(confirm)`).
- Exact nanopb release to pin. Pick the latest stable 0.4.x at implementation and record it here.
