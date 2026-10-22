# ESP32 BLE Mesh IoT Node — Specifications

Generic, config-driven BLE mesh node firmware for the ESP32. A JSON config stored in flash says which sensors and actuators are attached. At boot the firmware loads it, starts the matching drivers and exposes each device on the mesh using standard Bluetooth SIG models.

Each spec below is one implementation chunk. Implement them in order and respect `Depends on`. Update the **Status** column (and the `Status:` line inside the spec) as work progresses: `todo` → `in-progress` → `done`.

## Requirements summary

### Use cases (v1)
1. **Config upload:** a PC tool sends a JSON config over USB serial. The firmware validates it and stores it in flash. It takes effect after a reboot.
2. **Config-driven boot:** at boot the firmware loads the config and starts drivers for the configured devices (SHTC3, PIR, LED). If the config is missing or invalid, it logs the error and starts no drivers. The serial CLI stays available so a new config can be uploaded.
3. **Mesh provisioning:** a Python script on a Linux PC, using BlueZ `bluetooth-meshd`, provisions the node and configures its app keys and publish/subscribe addresses. The script stands in for the future embedded Linux gateway.
4. **Read and control:** the script receives SHTC3 temperature/humidity readings and PIR motion events, and switches the LED on and off. This also works when a second node relays the messages.
5. **Maintenance:** factory reset (serial command or BOOT-button long press) wipes the config and mesh membership. A task watchdog restarts a hung node, and the reset reason and a crash counter are logged.

### Hardware
- 2+ **ESP32-DevKitC** (classic ESP32, BLE 4.2) with a **WROVER** module (16 MB flash, 8 MB PSRAM), programmed and logged over USB serial only. No JTAG. PSRAM occupies GPIO 16/17.
- **SHTC3** temperature/humidity sensor (I2C).
- **PIR** motion sensor (digital GPIO input). The exact module is not yet chosen.
- **LED** (GPIO output, external).
- Gateway stand-in: a **Linux PC** with a Bluetooth adapter dedicated to `bluetooth-meshd`.
- Power: USB/mains. No sleep modes. The node acts as a mesh relay.

### Toolchain
- **ESP-IDF** v5.4.4 (pinned; mesh APIs differ between versions): FreeRTOS, ESP-BLE-MESH (Bluedroid host), cJSON, `esp_console`, NVS.
- **Python 3** tools on the PC: config upload and the gateway script.

### Mesh mapping
| Device | SIG model | Data |
|---|---|---|
| SHTC3 | Sensor Server (+ Sensor Setup Server) | Present Ambient Temperature, Present Ambient Relative Humidity |
| PIR | Sensor Server | Motion Sensed |
| LED | Generic OnOff Server | On/Off |

Each sensor's sampling and reporting intervals are set in the JSON config. The PIR publishes immediately on a motion edge.

### Simulation strategy
- **Host unit tests** (CMake + Unity, run in CI): config parsing and validation, driver framework, reporting logic, and the SHTC3 protocol against a mock HAL.
- **Espressif QEMU (ESP32):** boot, serial CLI, config storage, safe mode and factory reset. QEMU does not emulate Bluetooth, so the QEMU build disables mesh.
- **Real hardware only:** BLE mesh, the SHTC3, the PIR, and multi-node relay.

### Constraints
- Runs continuously on USB/mains power. Must survive bad configs and unexpected resets.
- A config is at most 4 KB.

### Out of scope for v1 (later)
- **Config delivery over the mesh:** needs a custom vendor model and chunked transfer to fit the ~380-byte mesh message limit. Once this exists, a node with no valid config should wait for one over the mesh.
- The embedded Linux gateway itself.
- OTA updates, secure boot and flash encryption.
- ADC and PWM drivers, and low-power operation.

### Project-wide open questions
- **Config changes vs. mesh composition:** a config that adds or removes devices changes the node's composition data (elements and models). The v1 rule is that such a change requires factory reset and reprovisioning. The firmware detects the mismatch and warns (see spec 10). The Mesh 1.1 Composition Data refresh is a possible later improvement.
- **Pins:** the pins in the example config are placeholders. Confirm them against your wiring and the ESP32 datasheet.

## Repository layout (target)

Components are created by the spec that implements them, so the repository grows into this layout chunk by chunk.

```
firmware/                 ESP-IDF project
  main/                   app entry, boot sequence
  components/
    node_hal/             thin hardware abstraction (GPIO, I2C, time) + ESP-IDF impl
    node_config/          JSON parse + validate → node_config_t
    config_store/         NVS persistence of the raw config
    node_cli/             esp_console commands
    drivers/              driver framework + pir, led, shtc3
    node_health/          watchdog, reset reason, crash counter, factory reset
    node_mesh/            ESP-BLE-MESH node, composition, model mapping
  test/host/              host unit tests (CMake + Unity + mock HAL)
  sdkconfig.defaults      hardware build
  sdkconfig.qemu          QEMU build (mesh disabled)
configs/                  example configs + JSON schema
tools/
  config_upload.py        PC → node config upload over serial
  gateway/                BlueZ mesh gateway stand-in script
```

## Chunks

| # | Spec | Depends on | Status |
|---|---|---|---|
| 01 | [Project skeleton](01-project-skeleton.md) | none | in-progress |
| 02 | [HAL abstraction](02-hal.md) | 01 | done |
| 03 | [Config schema & parser](03-config-parser.md) | 01 | done |
| 04 | [Config storage](04-config-storage.md) | 03 | todo |
| 05 | [Serial CLI & PC upload tool](05-serial-cli.md) | 04 | todo |
| 06 | [Driver framework](06-driver-framework.md) | 02, 03 | todo |
| 07 | [GPIO drivers: PIR & LED](07-gpio-drivers.md) | 06 | todo |
| 08 | [SHTC3 driver](08-shtc3-driver.md) | 06 | todo |
| 09 | [Watchdog, reset reason & factory reset](09-health-factory-reset.md) | 02, 04, 05 | todo |
| 10 | [Mesh node core](10-mesh-node-core.md) | 03, 09 | todo |
| 11 | [Mesh model mapping](11-mesh-model-mapping.md) | 06, 07, 08, 10 | todo |
| 12 | [PC gateway script (BlueZ)](12-pc-gateway-script.md) | 10, 11 | todo |
| 13 | [System tests](13-system-tests.md) | all | todo |

```
01 ──► 02 ──────────────┐
 └───► 03 ──► 04 ──► 05 │
        │      │     │  ▼
        │      └─────┴► 09 ──► 10 ──┐
        └──► 06 ◄── 02              ▼
              ├──► 07 ──────────►  11 ──► 12 ──► 13
              └──► 08 ──────────►
```
