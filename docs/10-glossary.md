# Glossary

Quick definitions for the terms used across these docs. Each one links to the
page where it matters most.

| Term | What it means |
|---|---|
| **Active object (AO)** | A thread with its own private event queue and timers, which handles one event at a time and shares no state with other threads. Every thread in our firmware is one. See [Active objects](03-active-objects.md). |
| **Run to completion** | An AO finishes handling one event before starting the next, and never blocks while doing it. |
| **Time event** | An AO's own timer. When it fires, it arrives as a normal event in that AO's queue. |
| **Publish / subscribe** | How AOs share data that changes: the owner publishes an event, and every subscriber gets a copy in its own queue. |
| **Command** | An event posted directly to the single AO that owns a resource, e.g. "switch the LED on". |
| **Config** | The JSON file that says what's plugged into a node. See [Configuration](05-configuration.md). |
| **Device** | A sensor or actuator listed in the config (SHTC3, PIR, LED). The on-board status LED and reset button are *not* devices. |
| **Device index** | A device's position in the config's device list. Used by the device layer, the mesh messages and the gateway to name a device. |
| **Driver** | The plugin that knows how to talk to one type of device. See [Devices](06-devices.md). |
| **FAULT** | A device state meaning it failed to initialise or failed several samples in a row. Faulted devices are retried, and report no values. |
| **Reporting policy** | The rules that decide when a sensor reading is worth publishing: first value, big enough change after a minimum gap, or a heartbeat after a maximum gap. |
| **Normal mode** | The node booted with a valid config and runs its drivers. See [Boot and modes](04-boot-and-modes.md). |
| **Safe mode** | The node booted without a usable config. Health, status LED and mesh run, drivers don't. |
| **HAL** | The thin hardware layer (`node_hal`) for GPIO, I2C and time. Swapped for a fake in host tests. |
| **Flavour** | One of the ways the firmware is built: `hw` (real board, mesh on) or `qemu` (emulator, mesh off). Host tests are a third, ESP-IDF-free build. |
| **Partition** | A named region of the flash chip. We care about `factory` (firmware), `config` (the config) and `nvs` (mesh state, crash counter). |
| **NVS** | ESP-IDF's key-value store in flash. Holds mesh keys and address, and the crash counter. |
| **LittleFS** | A small filesystem used in the `config` partition. Renames are atomic, which makes safe config saves possible. |
| **BLE mesh** | Bluetooth Low Energy networking where nodes relay messages for each other. See [Mesh networking](07-mesh.md). |
| **Provisioning** | Adding a device to a mesh network: it gets keys and an address. |
| **PB-ADV** | Provisioning over Bluetooth advertising packets. The only provisioning method our nodes accept. |
| **Relay** | A node that re-sends mesh messages so they reach nodes out of the sender's range. All our nodes relay. |
| **Element / model / composition** | Mesh building blocks: a node has elements, elements have models, and the composition is the list of both. Ours is one element, fixed. |
| **Vendor model** | A custom mesh model. Ours is the Stone Ocean Node Server, which carries every device's messages. |
| **Opcode** | The number at the start of a mesh message that says which message it is. |
| **Unsegmented message** | A mesh message small enough for one radio packet: at most 8 bytes of payload for our vendor messages. |
| **Protocol Buffers (protobuf)** | A compact, versionable binary format for messages. Our mesh messages are defined in `proto/`. |
| **nanopb** | A protobuf implementation for small microcontrollers. It uses no heap. |
| **Gateway** | The Linux machine that provisions nodes and talks to them over the mesh, using BlueZ's mesh daemon. |
| **Factory reset** | Wipe the config, the mesh membership and the crash counter. See [Health and recovery](08-health-and-recovery.md). |
| **Task watchdog (TWDT)** | An ESP-IDF timer that resets the chip if a registered thread stops checking in for 10 s. |
| **Status LED** | The on-board LED on GPIO0 that blinks a 1 s on / 1 s off heartbeat while the app runs. |
| **QEMU** | An emulator that runs the real ESP32 firmware on a PC, minus Bluetooth. See [Testing strategy](09-testing-strategy.md). |
