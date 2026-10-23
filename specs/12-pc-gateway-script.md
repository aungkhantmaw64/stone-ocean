# 12 — PC gateway script (BlueZ)

**Status:** todo
**Depends on:** 10, 11
**Target:** Linux PC · BlueZ `bluetooth-meshd` · Python 3 · `tools/gateway/`

## Goal
Provide a Python command-line tool that stands in for the future embedded Linux gateway. It provisions nodes, configures them, lists their devices, reads sensors, receives published events and switches the LED, all through BlueZ's mesh daemon and the Stone Ocean vendor model (spec 11). It should be reusable as the base of the real gateway and of our own node-management tool.

## Context
- The future gateway is an embedded Linux device that will most likely also use BlueZ mesh, so code written against the `org.bluez.mesh` D-Bus API should carry over.
- The node side is specs 10 and 11. The node is provisioned over **PB-ADV**. Its composition is element 0 only, with the Stone Ocean Node Server vendor model (CID `0x02E5`, model `0x0001`), whatever its config.
- Device data travels as Protocol Buffers messages defined in `proto/stone_ocean/mesh/v1/node.proto` (spec 11). The tool generates its Python code from that file with the pinned `protobuf` compiler; it never hand-codes the encoding.
- Later this tool will also use the same vendor model for the heartbeat, node commands, config delivery and OTA (future specs, out of scope).

## Hardware details
- A Linux machine with a Bluetooth adapter: the stone-ocean-gateway BeagleBone Black image (BlueZ 5.86, the bench setup used for spec 10), or a PC. `bluetooth-meshd` needs exclusive control of its adapter. Either:
  - use a dedicated USB Bluetooth dongle, or
  - stop `bluetooth.service` while `bluetooth-meshd` runs.
- BlueZ version: use a recent release with the `org.bluez.mesh` D-Bus API (see `doc/mesh-api.txt` in BlueZ). Record the tested version in the tool's README.

## Scope
- In scope:
  - Python package `tools/gateway/` using a D-Bus library (`dbus-next` recommended, asyncio). BlueZ's `test/test-mesh` and `tools/mesh-cfgclient` are reference implementations.
  - Gateway "application" registered with `bluetooth-meshd`. It has one element with:
    - Config Client (SIG 0x0001)
    - Health Client (SIG 0x0003)
    - Stone Ocean Node Client, vendor model (CID `0x02E5`, model `0x0002`), declared in the element's `VendorModels`
  - Local state file `~/.config/stone-ocean-gateway/state.json`: network token, nodes (UUID, unicast address, and the last `DeviceList`: device index, type and id).
  - Subcommands:

    | Command | Action |
    |---|---|
    | `init` | Create the mesh network (CreateNetwork) and app key 0 on net key 0. |
    | `scan [--timeout 10]` | List unprovisioned device UUIDs. |
    | `provision <uuid>` | Provision, then Config: get Composition Data, check that the Stone Ocean Node Server is on element 0, add app key 0, bind it to that model, and set its publication to group `0xC000` (configurable). Then send `DeviceListGet` and save the node with its devices. |
    | `nodes` | List known nodes with their devices (index, type, id). |
    | `devices <node>` | Send `DeviceListGet`, print the `DeviceList` and update the saved devices. |
    | `get <node> [<device>]` | Send `DeviceGet` for one device (index or id) or, without one, every device. Print each decoded `DeviceStatus`. |
    | `set <node> <device> on\|off [--unack]` | Send `OnOffSet` (or `OnOffSetUnack`). Print the returned `DeviceStatus`. |
    | `listen` | Subscribe to group `0xC000` and print every decoded `DeviceStatus` as it arrives, stamped with the arrival time and the device id. |
    | `remove <node>` | Config Node Reset, then drop the node from state. |
  - Message encode/decode on the generated protobuf code, kept in one module shared with the tests: opcode ↔ message type, scaling of `temperature_dc` and `humidity_dpct` to °C and %RH, and "unknown" for absent value fields.
  - `tools/gateway/README.md`: daemon setup, permissions (D-Bus policy), and a typical session.
- Out of scope: heartbeat, node commands, config transfer and OTA over the vendor model (future specs), a web UI, MQTT/cloud bridging (future gateway work).

## Interface
```
python -m gateway init
python -m gateway scan
python -m gateway provision <uuid> [--group 0xC000]
python -m gateway devices 0x0002
python -m gateway get 0x0002 env1
python -m gateway set 0x0004 2 on
python -m gateway listen
```
Python module boundaries: `mesh_app.py` (D-Bus application/element objects), `config_client.py`, `node_client.py` (Stone Ocean vendor messages: opcodes, protobuf encode/decode, value scaling), `state.py`, `cli.py`. The generated protobuf module is built from `proto/` and not committed.

## Behavior & constraints
- All D-Bus calls have timeouts. Retry config messages up to 3 times.
- An unknown vendor opcode, or a payload that does not decode, is printed as raw hex instead of raising an error. Unknown protobuf fields from a newer node are ignored.
- A `get` or `set` with no reply within the timeout exits with an error naming the node and device: the node does not reply to bad requests (spec 11).
- The tool must be usable unattended, for the system tests in spec 13: non-interactive, exit code 0 on success, and `--json` output on `get`, `nodes` and `listen`.

## Verification
- **Simulation/host:** pytest for `node_client.py` encode/decode, using the byte vectors in `proto/testdata/` that the firmware's host tests also check (spec 11), and for `state.py`. D-Bus is not needed for these.
- **On hardware:**
  - Run `init`, then `scan` (the node's UUID shows up).
  - `provision` succeeds.
  - `nodes` and `devices` show the node's configured devices.
  - `get` on the SHTC3 prints plausible values.
  - `set` toggles the LED.
  - `listen` shows motion within 1 s and periodic SHTC3 reports.

## Acceptance criteria
- [ ] `init`, `scan`, `provision`, `nodes`, `devices`, `get`, `set`, `listen` and `remove` work against a real node.
- [ ] Provisioning automatically binds the app key to the Stone Ocean Node Server, sets its publication and saves the node's device list.
- [ ] Decoded values match the readings in the node's log.
- [ ] `--json` output is available for scripting, and exit codes are meaningful.
- [ ] Encode/decode logic is covered by pytest with the vectors in `proto/testdata/`, shared with the firmware tests.
- [ ] README documents BlueZ version, daemon setup and adapter exclusivity.

## Open questions
- ~~The BlueZ version on the developer PC, and whether a dedicated USB adapter is available.~~ The bench gateway is the stone-ocean-gateway BeagleBone Black image with BlueZ 5.86 and a USB BLE dongle; the development laptop cannot run `bluetooth-meshd` (spec 10).
- Where the tool runs: on the BeagleBone Black itself (Python and its dependencies added to the Yocto image), or on a PC talking to its `bluetooth-meshd`.
