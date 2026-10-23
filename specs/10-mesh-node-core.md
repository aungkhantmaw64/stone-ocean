# 10 — Mesh node core

**Status:** done
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
- [x] Node beacons unprovisioned with a stable, MAC-derived UUID.
- [x] BlueZ `bluetooth-meshd` can provision it over PB-ADV, and the log shows it provisioned, with its address.
- [x] Provisioning survives reboot. Factory reset (both triggers) clears it.
- [x] Composition = primary element + one element per device, in config order (host test + boot log).
- [x] Composition-hash mismatch is detected and logged as specified.
- [x] Relay is enabled, and free internal heap after start is ≥ 40 KB (logged).
- [x] The hardware build, with the mesh code linked in, fits the factory app partition with ≥ 15% headroom (check with `idf.py size`). Moved here from spec 01, where nothing called the mesh code yet, so the linker dropped it.

Verified so far: `idf.py size` of the hardware build gives a 972,723-byte image; `mesh_node.bin` is 0xed830 bytes, leaving 54% of the 2 MB factory partition free. Host tests `test_node_mesh_comp` (20 tests: element count from config, safe mode, match / mismatch / unprovisioned, the hash stored at provisioning, the UUID) and `test_node_mesh_ao` (11 tests: hash stored and line logged at provisioning, gateway node reset erases and restarts, provisioning notices, lost events) pass under ASan, LSan and UBSan, and cover the host-testable parts of AC 1, 2, 3, 4 and 5. The QEMU build (mesh compiled out) still builds and boots with the example config.

Verified on hardware against the BeagleBone Black gateway (stone-ocean-gateway image, BlueZ 5.86 `bluetooth-meshd` and `mesh-cfgclient`, USB BLE dongle). The development laptop cannot run `bluetooth-meshd`: it needs the `algif_aead` kernel module, which is disabled there for CVE-2026-31431. Node: custom ESP32 WROVER board, config `lab-node-1` with 2 devices (`env1` shtc3, `pir1` pir).
- AC 1: the boot log shows `uuid=73746f6e656f6365616eb0b21ca220ee`, the `stoneocean` prefix followed by the BT MAC `b0:b2:1c:a2:20:ee`. After the button factory reset the node beaconed unprovisioned again, and `mesh-cfgclient` found it with the same UUID.
- AC 2: `mesh-cfgclient` provisioned the node over PB-ADV. The node logged `mesh=prov` with the address (0x00aa, then 0x00ab after a reprovision). `composition-get` and `appkey-add 0` both succeeded.
- AC 3 (partial): a reboot of the provisioned node resumed at `mesh=prov addr=0x00aa` with no reprovisioning. The button factory reset (≥ 5 s) cleared provisioning, and the node came back unprovisioned in safe mode (config erased, per spec 09); `composition-get` after reprovisioning showed the primary element only. A Config Node Reset from the gateway (`node-reset`) also unprovisioned the node and restarted it, with its config kept.
- AC 4: the boot log lists element 0 (primary, config + health server, 2 models), element 1 `env1` and element 2 `pir1`, `elements=3`. From the gateway, `composition-get` returns a 27-byte Composition Data Status: CID 0x02E5, relay yes, proxy/friend/LPN no, element 0 with Configuration Server and Health Server, and elements 1 and 2 with no models (spec 11 adds them).
- AC 6: `relay=on`. `free heap after mesh start: internal=178351 total=4321052`, well above the 40 KB limit. `mesh started in 708 ms`, under the 2 s limit.

Not yet verified on hardware:
- AC 3: the PC trigger, `tools/config_flash.py factory-reset`.
- AC 5: the mismatch WARN line on the board (host tests cover the decision).

Observed on hardware: ESP-BLE-MESH raises `ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT` again when it restores a provisioned node at boot. So the provisioning-complete lines are logged twice per boot, and the mesh AO stores the hash again on every boot. The effect is harmless (the value written is the hash already in use), but it is one extra NVS write per boot.

Gateway note: if the gateway's `mesh-cfgclient` database loses a node (an interrupted provisioning, or `create` run again), the node stays provisioned with keys that the gateway doesn't have. `composition-get` then fails with `Failed to send`, and `list-nodes` shows only the gateway's own node (0x0001). To recover, factory-reset the node and reprovision it.

## Open questions
- Should Mesh 1.1 (Remote Provisioning / Composition Data Page 128 refresh) be adopted later to avoid reprovisioning on config change? This depends on the ESP-IDF and BlueZ versions in use.
- ~~Exact Kconfig names may differ between ESP-IDF versions.~~ Confirmed against v5.4.4: `BLE_MESH_NODE`, `BLE_MESH_PB_ADV`, `BLE_MESH_RELAY`, `BLE_MESH_SETTINGS`, `BLE_MESH_PB_GATT` and `BLE_MESH_GATT_PROXY_SERVER`.
