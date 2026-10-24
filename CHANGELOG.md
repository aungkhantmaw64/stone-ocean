# Changelog

All notable changes to the node firmware are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-10-24

First release of the config-driven BLE mesh node firmware for the ESP32-DevKitC
(ESP-IDF v5.4.4). A JSON config stored in flash says which sensors and actuators
are attached; the node joins a BLE mesh and reports them through a vendor model.

### Added

- **Config-driven devices.** The node reads a JSON config (format `version: 1`)
  that declares I2C buses and SHTC3, PIR and LED devices, fills in defaults, and
  rejects invalid configs with the JSON path of the problem. A JSON schema and
  example configs ship in `configs/`. Configs may not use the status LED or
  reset-button pins. (#3, #12)
- **Config storage in flash.** The config lives on a dedicated 64 KB LittleFS
  `config` partition with CRC checks. At boot the node loads it and starts
  normally, or enters safe mode and logs why once if the config is missing or
  invalid. (#4)
- **PC config flashing tool** (`tools/config_flash.py`). Checks a config with the
  firmware's own parser, then writes, reads back or erases the config partition
  over USB without reflashing the firmware. (#5)
- **Sensors and actuators.** Devices are sampled on their configured interval and
  readings are reported only when they change enough. Faulty devices are isolated
  and retried without stopping the rest of the node. Drivers: PIR motion sensor,
  LED (on/off), and SHTC3 temperature and humidity sensor over I2C, with ID and
  CRC checks. (#9, #10, #13)
- **Status LED.** The board LED on GPIO0 blinks a 1 s heartbeat from boot,
  before the config is loaded. (#12)
- **Health and factory reset.** The node logs the reset reason and counts crashes
  in NVS, and the task watchdog monitors every task. Holding the reset button for
  5 s performs a factory reset that erases the stored config and mesh state, and
  `config_flash.py` can trigger the same reset from the PC. (#15)
- **BLE mesh node.** The node comes up on ESP-BLE-MESH with a device UUID derived
  from its MAC, records provisioning, and handles node reset from the
  provisioner. Its primary element has the Configuration and Health servers. (#16)
- **Vendor model.** A protobuf-encoded vendor model (schema in
  `proto/stone_ocean/mesh/`) publishes device value changes and answers get and
  on/off set requests, so a provisioner can read sensors and switch the LED. (#17)
- **HAL self-test** that can be enabled from Kconfig, for checking a new board's
  GPIO and I2C wiring. (#1, #7)

[Unreleased]: https://github.com/aungkhantmaw64/stone-ocean/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/aungkhantmaw64/stone-ocean/releases/tag/v0.1.0
