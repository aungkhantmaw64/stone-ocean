# 10 — Mesh node core

**Status:** todo
**Depends on:** 03, 09
**Target:** ESP32-DevKitC · ESP-IDF v5.x ESP-BLE-MESH (Bluedroid) · `firmware/components/node_mesh`

## Goal
Bring up the node as an unprovisioned BLE mesh device that the BlueZ gateway script (12) can provision over PB-ADV. It acts as a relay and keeps its mesh state across reboots. Its composition data (elements and models) is built from the active config.

## Context
- Only compiled when `CONFIG_NODE_MESH_ENABLE=y` (the hardware build). The QEMU build stubs this component out.
- Builds composition from `node_config_t`. Spec 11 attaches the per-device model behaviour (Sensor/OnOff server callbacks) to the models created here.
- Registers a factory-reset hook (spec 09) that clears provisioning data.
- Starts in safe mode too. In that case the composition contains only the primary element.

## Hardware details
Classic ESP32 radio, BLE 4.2. Bluetooth controller in BLE-only mode. Bluetooth Classic memory is released (`esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)`).

## Scope
- In scope:
  - ESP-BLE-MESH init with the Kconfig options:
    - `BLE_MESH_NODE`
    - `BLE_MESH_PB_ADV` (required: BlueZ `bluetooth-meshd` provisions over PB-ADV)
    - `BLE_MESH_RELAY`
    - `BLE_MESH_SETTINGS` (persistent mesh state)
    - optional `BLE_MESH_PB_GATT` / `BLE_MESH_GATT_PROXY_SERVER` (for phone-app debugging)
  - Device UUID: 16 bytes derived from the BT MAC address (fixed prefix + MAC), stable across reboots and factory resets.
  - Composition layout:
    - **Element 0 (primary):** Config Server and Health Server. No device models.
    - **Elements 1..N:** one element per configured device, in config order. Models are added in spec 11.
  - Static allocation for up to `1 + NODE_CFG_MAX_DEVICES` elements. Composition is built at boot before `esp_ble_mesh_init`.
  - Composition-change detection:
    - Store `node_config_composition_hash()` (spec 03) in NVS (namespace `meshmeta`) at provisioning-complete.
    - On boot, if the node is provisioned and the hash differs from the active config's, log a WARN: `config composition changed since provisioning; factory-reset and reprovision required`. Run with the **stored-hash-compatible primary element only** (device elements disabled) until reprovisioned.
  - Provisioning callbacks logged. OOB: none (no input/output) for v1.
  - CLI: `status` shows `mesh=unprov|prov`, unicast address, element count and relay state. A `mesh-info` command prints the UUID, IV index, net/app key indexes and the composition.
  - Factory-reset hook: `esp_ble_mesh_node_local_reset()` and erase `meshmeta`.
- Out of scope: model message handling (11), vendor config model (future), Low Power / Friend features.

## Interface
```c
int  mesh_node_start(const node_config_t *cfg /* NULL in safe mode */);
bool mesh_node_is_provisioned(void);
uint16_t mesh_node_primary_addr(void);
/* For spec 11: element/model handles for device index i (element 1+i) */
esp_ble_mesh_elem_t *mesh_node_device_element(int dev_index);
/* Spec 11 fills model arrays for each device element before start: */
typedef int (*mesh_models_builder_t)(int dev_index, const node_dev_cfg_t *cfg,
                                     esp_ble_mesh_model_t **sig_models, uint8_t *sig_count);
void mesh_node_set_models_builder(mesh_models_builder_t builder);
```

## Behavior & constraints
- Starting mesh must not block boot for more than 2 s. The CLI must be responsive before mesh init finishes.
- Unprovisioned: send unprovisioned beacons until provisioned.
- After provisioning: persist state, and enable relay (the node is mains powered).
- A reboot of a provisioned node resumes with the same unicast address and keys, with no reprovisioning.
- Mesh tasks run at ESP-IDF default priorities. The devices task must stay below them (spec 06).
- RAM: measure free heap after mesh start and log it. Fail the acceptance check if it is below 40 KB.

## Verification
- **Simulation/host:** composition-builder logic tested with a mocked mesh layer: element count from config, hash comparison decision (match / mismatch / unprovisioned).
- **QEMU:** the QEMU build (mesh disabled) still compiles and boots.
- **On hardware:**
  - Flash, then provision with the gateway script (spec 12). If spec 12 isn't ready, use BlueZ `mesh-cfgclient` from the CLI, or the nRF Mesh app if PB-GATT is enabled.
  - `mesh-info` shows the address and keys.
  - The node stays provisioned after a reboot.
  - Factory reset → the node is unprovisioned again and beacons.
  - Change the config to add a device, reboot → the WARN log appears and device elements are disabled.

## Acceptance criteria
- [ ] Node beacons unprovisioned with a stable, MAC-derived UUID.
- [ ] BlueZ `bluetooth-meshd` can provision it over PB-ADV, and it appears in `mesh-info`/`status`.
- [ ] Provisioning survives reboot. Factory reset (both triggers) clears it.
- [ ] Composition = primary element + one element per device, in config order (host test + `mesh-info`).
- [ ] Composition-hash mismatch is detected and logged as specified.
- [ ] Relay is enabled, and free heap after start is ≥ 40 KB (logged).

## Open questions
- Should Mesh 1.1 (Remote Provisioning / Composition Data Page 128 refresh) be adopted later to avoid reprovisioning on config change? This depends on the ESP-IDF and BlueZ versions in use.
- Exact Kconfig names may differ slightly between ESP-IDF versions. Confirm them against the pinned version.
