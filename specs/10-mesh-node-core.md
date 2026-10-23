# 10 — Mesh node core

**Status:** in-progress
**Depends on:** 03, 09
**Target:** custom ESP32 WROVER board · ESP-IDF v5.4.4 ESP-BLE-MESH (Bluedroid) · `firmware/components/node_mesh`

## Goal
Bring up the node as an unprovisioned BLE mesh device that the BlueZ gateway script (12) can provision over PB-ADV. It acts as a relay and keeps its mesh state across reboots. Its composition data (elements and models) is built from the active config.

## Context
- Only compiled when `CONFIG_NODE_MESH_ENABLE=y` (the hardware build). The QEMU build stubs this component out.
- Builds composition from `node_config_t`. Spec 11 attaches the per-device model behaviour (Sensor/OnOff server callbacks) to the models created here.
- Registers a factory-reset hook (spec 09) that clears provisioning data.
- Starts in safe mode too. In that case the composition contains only the primary element.
- A small **mesh AO** (spec 14) owns the `meshmeta` state. The ESP-BLE-MESH callbacks run in Bluedroid's task and only post events to it; the AO writes NVS and logs.

## Hardware details
Classic ESP32 radio, BLE 4.2. Bluetooth controller in BLE-only mode. Bluetooth Classic memory is released (`esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)`).

## Scope
- In scope:
  - ESP-BLE-MESH init with the Kconfig options:
    - `BLE_MESH_NODE`
    - `BLE_MESH_PB_ADV` (required: BlueZ `bluetooth-meshd` provisions over PB-ADV)
    - `BLE_MESH_RELAY`
    - `BLE_MESH_SETTINGS` (persistent mesh state)
    - `BLE_MESH_PB_GATT` and `BLE_MESH_GATT_PROXY_SERVER` are off (PB-ADV only). ESP-IDF turns the proxy server on by default, so `sdkconfig.defaults` turns it off. `BLE_MESH_SETTINGS` is off by default and is turned on there.
  - Device UUID: 16 bytes, the ASCII prefix `stoneocean` (10 bytes) followed by the 6-byte BT MAC address. Stable across reboots and factory resets.
  - Company ID (CID) in the composition data: Espressif, `0x02E5`.
  - Composition layout:
    - **Element 0 (primary):** Config Server and Health Server. No device models.
    - **Elements 1..N:** one element per configured device, in config order. Models are added in spec 11.
  - Static allocation for up to `1 + NODE_CONFIG_MAX_DEVICES` elements. Composition is built at boot before `esp_ble_mesh_init`.
  - Composition-change detection:
    - Store `node_config_composition_hash()` (spec 03) of the composition in use in NVS (namespace `meshmeta`) at provisioning-complete: the active config's hash when the device elements are present, or the hash of a config with no devices when only the primary element is (safe mode, or a mismatch).
    - Erase the stored hash when the node leaves the network: factory reset, or a Config Node Reset from the gateway. After a Config Node Reset the node also restarts, so that it is reprovisioned with a composition built afresh from the active config.
    - The composition is fixed before `esp_ble_mesh_init()`, which is when the mesh restores its own provisioning state. So at boot a stored hash stands for "provisioned".
    - On boot in normal mode, if a hash is stored and differs from the active config's, log a WARN: `config composition changed since provisioning; factory-reset and reprovision required`. Run with the **stored-hash-compatible primary element only** (device elements disabled) until reprovisioned.
    - In safe mode the hash is not compared; the composition is the primary element only.
  - Provisioning callbacks logged. OOB: none (no input/output) for v1.
  - Mesh log lines: at boot and when provisioning completes, log `mesh=unprov|prov`, the unicast address, the element count and the relay state. At boot, also log the UUID, IV index, net/app key indexes and the composition.
  - Factory-reset hook: `esp_ble_mesh_node_local_reset()` and erase `meshmeta`.
- Out of scope: model message handling (11), vendor config model (future), Low Power / Friend features.

## Interface
```c
typedef enum {
  NODE_MESH_OK = 0,
  NODE_MESH_ERR_ARG,    /* bad argument */
  NODE_MESH_ERR_STATE,  /* already started, or not started */
  NODE_MESH_ERR_BT,     /* Bluetooth controller or host failed to start */
  NODE_MESH_ERR_MESH,   /* ESP-BLE-MESH init or provisioning enable failed */
} node_mesh_err_t;

node_mesh_err_t node_mesh_start(const node_config_t *cfg /* NULL in safe mode */);
bool     node_mesh_is_provisioned(void);
uint16_t node_mesh_primary_addr(void);
/* For spec 11: element/model handles for device index i (element 1+i);
 * NULL if that device has no element (out of range, or composition mismatch) */
esp_ble_mesh_elem_t *node_mesh_device_element(int dev_index);
/* Spec 11 fills model arrays for each device element before start: */
typedef node_mesh_err_t (*node_mesh_models_builder_t)(int dev_index, const node_config_dev_t *cfg,
                                                      esp_ble_mesh_model_t **sig_models, uint8_t *sig_count);
node_mesh_err_t node_mesh_set_models_builder(node_mesh_models_builder_t builder);
```

## Behavior & constraints
- Starting mesh must not block boot for more than 2 s.
- Unprovisioned: send unprovisioned beacons until provisioned.
- After provisioning: persist state, and enable relay (the node is mains powered).
- A reboot of a provisioned node resumes with the same unicast address and keys, with no reprovisioning.
- Mesh tasks run at ESP-IDF default priorities. The devices AO must stay below them (spec 06).
- RAM: measure free heap after mesh start and log it, internal RAM and total. Fail the acceptance check if free internal RAM is below 40 KB (PSRAM does not count).

## Verification
- **Simulation/host:** composition-builder logic tested with a mocked mesh layer: element count from config, hash comparison decision (match / mismatch / unprovisioned).
- **QEMU:** the QEMU build (mesh disabled) still compiles and boots.
- **On hardware:**
  - Flash, then provision with the gateway script (spec 12). If spec 12 isn't ready, use BlueZ `mesh-cfgclient` from the CLI, or the nRF Mesh app if PB-GATT is enabled.
  - The log shows the address and key indexes.
  - The node stays provisioned after a reboot.
  - Factory reset → the node is unprovisioned again and beacons.
  - Change the config to add a device, reboot → the WARN log appears and device elements are disabled.

## Acceptance criteria
- [ ] Node beacons unprovisioned with a stable, MAC-derived UUID.
- [ ] BlueZ `bluetooth-meshd` can provision it over PB-ADV, and the log shows it provisioned, with its address.
- [ ] Provisioning survives reboot. Factory reset (both triggers) clears it.
- [ ] Composition = primary element + one element per device, in config order (host test + boot log).
- [ ] Composition-hash mismatch is detected and logged as specified.
- [ ] Relay is enabled, and free internal heap after start is ≥ 40 KB (logged).
- [x] The hardware build, with the mesh code linked in, fits the factory app partition with ≥ 15% headroom (check with `idf.py size`). Moved here from spec 01, where nothing called the mesh code yet, so the linker dropped it.

Verified so far: `idf.py size` of the hardware build gives a 972,723-byte image; `mesh_node.bin` is 0xed830 bytes, leaving 54% of the 2 MB factory partition free. Host tests `test_node_mesh_comp` (20 tests: element count from config, safe mode, match / mismatch / unprovisioned, the hash stored at provisioning, the UUID) and `test_node_mesh_ao` (11 tests: hash stored and line logged at provisioning, gateway node reset erases and restarts, provisioning notices, lost events) pass under ASan, LSan and UBSan, and cover the host-testable parts of AC 1, 2, 3, 4 and 5. The QEMU build (mesh compiled out) still builds and boots with the example config.

Not yet verified on hardware: AC 1–6. They need a BlueZ provisioner, which cannot run on the development laptop (`bluetooth-meshd` needs the `algif_aead` kernel module, disabled there for CVE-2026-31431). They will be checked against a BeagleBone Black gateway with a USB BLE dongle. The boot-log parts of AC 1, 4 and 6 (UUID, element lines, heap, start time) need only the board.

## Open questions
- Should Mesh 1.1 (Remote Provisioning / Composition Data Page 128 refresh) be adopted later to avoid reprovisioning on config change? This depends on the ESP-IDF and BlueZ versions in use.
- ~~Exact Kconfig names may differ between ESP-IDF versions.~~ Confirmed against v5.4.4: `BLE_MESH_NODE`, `BLE_MESH_PB_ADV`, `BLE_MESH_RELAY`, `BLE_MESH_SETTINGS`, `BLE_MESH_PB_GATT` and `BLE_MESH_GATT_PROXY_SERVER`.
