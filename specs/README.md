# ESP32 BLE Mesh IoT Node — Specifications

Generic, config-driven BLE mesh node firmware for the ESP32. A JSON config stored in flash says which sensors and actuators are attached. At boot the firmware loads it, starts the matching drivers and exposes each device on the mesh through the Stone Ocean vendor model, whose messages are Protocol Buffers.

Each spec below is one implementation chunk. Implement them in order and respect `Depends on`. Specs 14 and 15 were added after the others were numbered; implement them, in that order, before 06. Spec 16 was added later too; implement it after 14 and before 09. Update the **Status** column (and the `Status:` line inside the spec) as work progresses: `todo` → `in-progress` → `done`.

## Requirements summary

### Use cases (v1)
1. **Config flashing:** a PC tool validates a JSON config (JSON Schema plus the firmware's own parser, built for the PC) and writes it straight into the node's `config` flash partition over USB serial with esptool. The node runs no code to receive it. The config takes effect at the reset that follows the write.
2. **Config-driven boot:** at boot the firmware loads the config and starts drivers for the configured devices (SHTC3, PIR, LED). If the config is missing or invalid, it logs the error and starts no drivers. A new config can be flashed from the PC at any time.
3. **Mesh provisioning:** a Python script on a Linux PC, using BlueZ `bluetooth-meshd`, provisions the node and configures its app keys and publish/subscribe addresses. The script stands in for the future embedded Linux gateway.
4. **Read and control:** the script receives SHTC3 temperature/humidity readings and PIR motion events, and switches the LED on and off. This also works when a second node relays the messages.
5. **Maintenance:** factory reset (reset-button long press, or erasing the config and mesh partitions from the PC) wipes the config and mesh membership. A task watchdog restarts a hung node, and the reset reason and a crash counter are logged.

### Hardware
- 2+ **custom ESP32 boards** (classic ESP32, BLE 4.2) with a **WROVER** module (16 MB flash, 8 MB PSRAM), programmed and logged over USB serial only. No JTAG. PSRAM occupies GPIO 16/17. Early specs were brought up on an ESP32-DevKitC with the same module.
- **Status LED** on GPIO0, active low, fixed on the board (spec 16). Blinks a 1 s on / 1 s off heartbeat while the app runs. Not a config device and not on the mesh.
- **Reset button** on GPIO34, fixed on the board (spec 09).
- **SHTC3** temperature/humidity sensor (I2C).
- **PIR** motion sensor (digital GPIO input). The exact module is not yet chosen.
- **LED** (GPIO output, external), configured in the JSON config and switched over the mesh.
- Gateway stand-in: a **Linux PC** with a Bluetooth adapter dedicated to `bluetooth-meshd`.
- Power: USB/mains. No sleep modes. The node acts as a mesh relay.

### Toolchain
- **ESP-IDF** v5.4.4 (pinned; mesh APIs differ between versions): FreeRTOS, ESP-BLE-MESH (Bluedroid host), cJSON, NVS, LittleFS (managed component `joltwallet/littlefs`).
- **Protocol Buffers** for every mesh message: the schema lives in `proto/` and is shared by the firmware (nanopb, pinned release, code generated at build time) and the gateway tool (Python `protobuf`).
- **Python 3** tools on the PC: config flashing (esptool, littlefs-python) and the gateway script.

### Mesh mapping
Nodes are managed only by our own tools, so the node uses no Bluetooth SIG device models (Sensor, Generic OnOff). Every device is reached through **one vendor model**, the Stone Ocean Node Server (CID `0x02E5` for now, model `0x0001`), on the primary element. Its messages are Protocol Buffers defined in `proto/stone_ocean/mesh/v1/node.proto`, one vendor opcode per message type (spec 11). Third parties talk to nodes through that schema.

| Device | Data in `DeviceStatus` | Commands |
|---|---|---|
| SHTC3 | `temperature_dc` (0.1 °C), `humidity_dpct` (0.1 %RH) | — |
| PIR | `motion` | — |
| LED | `on` | `OnOffSet` |

The node's composition is fixed (the primary element only), so a config that adds or removes devices needs no reprovisioning; the gateway reads the device list with `DeviceListGet`. Each sensor's sampling and reporting intervals are set in the JSON config. The PIR publishes immediately on a motion edge. Readings and commands fit one unsegmented mesh message (8 payload bytes).

### Simulation strategy
- **Host unit tests** (CMake + Unity, run in CI): config parsing and validation, driver framework, reporting logic, the SHTC3 protocol against a mock HAL, and mesh message encoding against byte vectors in `proto/testdata/` that the gateway's pytest checks too.
- **Espressif QEMU (ESP32):** boot, config storage, config flashing (into the QEMU flash image), safe mode and factory reset. QEMU does not emulate Bluetooth, so the QEMU build disables mesh.
- **Real hardware only:** BLE mesh, the SHTC3, the PIR, and multi-node relay.

### Constraints
- Runs continuously on USB/mains power. Must survive bad configs and unexpected resets.
- A config is at most 4 KB.
- The node has no command interface over serial. UART0 is used only for flashing and the log, and runtime state is reported in the log.
- **Concurrency: active objects only.** Every thread of the firmware is an active object built on `node_ao` (spec 14): a private event queue, a thread that runs one event at a time to completion, and time events. Firmware code never creates plain FreeRTOS tasks or timers, and threads share no state directly. They post commands to the AO that owns a resource, and data that changes is published (spec 15) to the AOs that subscribed to it, which keep their own copy. Tasks owned by ESP-IDF itself (Bluedroid, the timer service, idle, `app_main`) are exempt. ISRs and the callbacks ESP-IDF runs in its own tasks only post events to an AO.

### Out of scope for v1 (later)
- **More messages on the Stone Ocean vendor model** (opcodes reserved in spec 11):
  - a system-info heartbeat (firmware version, uptime, free heap, reset reason, crash counter, device faults), published every few minutes;
  - node commands: reboot, factory reset, identify;
  - **config delivery over the mesh**, with chunked transfer to fit the ~380-byte mesh message limit. Once this exists, a node with no valid config should wait for one over the mesh.
- **Air-quality sensors** (CO2, VOC, particulate matter), as new device types and `DeviceStatus` fields.
- The embedded Linux gateway itself.
- OTA updates (over the mesh — Mesh 1.1 BLOB/DFU or the vendor model — or over Wi-Fi), secure boot and flash encryption.
- ADC and PWM drivers, and low-power operation.

### Project-wide open questions
- ~~**Config changes vs. mesh composition.**~~ Closed by spec 11: the composition is fixed (the primary element only), so a config change needs no reprovisioning.
- **Company ID:** the vendor model uses Espressif's `0x02E5` until Stone Ocean has its own Bluetooth SIG company ID (spec 11).
- **Pins:** the pins in the example config are placeholders. Confirm them against your wiring and the ESP32 datasheet.

## Repository layout (target)

Components are created by the spec that implements them, so the repository grows into this layout chunk by chunk.

```
firmware/                 ESP-IDF project
  main/                   app entry, boot sequence
  components/
    node_hal/             thin hardware abstraction (GPIO, I2C, time) + ESP-IDF impl
    node_ao/              active object framework (event queues, time events) + FreeRTOS port
    node_config/          JSON parse + validate → node_config_t
    config_store/         LittleFS persistence of the raw config
    node_dev/             device driver framework (devices AO) + pir, led, shtc3
    node_health/          watchdog, reset reason, crash counter, factory reset
    node_mesh/            ESP-BLE-MESH node, Stone Ocean vendor model, protobuf messages (nanopb)
  test/host/              host unit tests (CMake + Unity + mock HAL)
  sdkconfig.defaults      hardware build
  sdkconfig.qemu          QEMU build (mesh disabled)
configs/                  example configs + JSON schema
proto/                    mesh message schema (.proto + nanopb options), shared by firmware and gateway
  testdata/               known-answer byte vectors checked by both test suites
tools/
  config_flash.py         PC → node config check, write, read and erase (esptool)
  node_config_check/      firmware config parser built as a PC program
  gateway/                BlueZ mesh gateway stand-in script
```

## Chunks

| # | Spec | Depends on | Status |
|---|---|---|---|
| 01 | [Project skeleton](01-project-skeleton.md) | none | done |
| 02 | [HAL abstraction](02-hal.md) | 01 | done |
| 03 | [Config schema & parser](03-config-parser.md) | 01 | done |
| 04 | [Config storage](04-config-storage.md) | 03 | done |
| 05 | [PC config flashing tool](05-config-flash-tool.md) | 04 | done |
| 06 | [Driver framework](06-driver-framework.md) | 02, 03, 14, 15 | done |
| 07 | [GPIO drivers: PIR & LED](07-gpio-drivers.md) | 06 | done |
| 08 | [SHTC3 driver](08-shtc3-driver.md) | 06 | done |
| 09 | [Watchdog, reset reason & factory reset](09-health-factory-reset.md) | 02, 04, 05 | done |
| 10 | [Mesh node core](10-mesh-node-core.md) | 03, 09 | done |
| 11 | [Mesh model mapping (vendor model, protobuf)](11-mesh-model-mapping.md) | 06, 07, 08, 10 | in-progress |
| 12 | [PC gateway script (BlueZ)](12-pc-gateway-script.md) | 10, 11 | todo |
| 13 | [System tests](13-system-tests.md) | all | todo |
| 14 | [Active object framework](14-active-objects.md) | 02 | done |
| 15 | [AO publish-subscribe](15-ao-publish-subscribe.md) | 14 | done |
| 16 | [Status LED](16-status-led.md) | 02, 03, 14 | done |

```
01 ──► 02 ──────────────┐
 └───► 03 ──► 04 ──► 05 │
        │      │     │  ▼
        │      └─────┴► 09 ──► 10 ──┐
        └──► 06 ◄── 15 ◄── 14 ◄── 02▼
              │            └──► 16 ◄── 03
              ├──► 07 ──────────►  11 ──► 12 ──► 13
              └──► 08 ──────────►
```
