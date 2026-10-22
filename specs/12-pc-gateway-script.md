# 12 — PC gateway script (BlueZ)

**Status:** todo
**Depends on:** 10, 11
**Target:** Linux PC · BlueZ `bluetooth-meshd` · Python 3 · `tools/gateway/`

## Goal
Provide a Python command-line tool that stands in for the future embedded Linux gateway. It provisions nodes, configures them, reads sensors, receives published events and switches the LED, all through BlueZ's mesh daemon. It should be reusable as the base of the real gateway.

## Context
- The future gateway is an embedded Linux device that will most likely also use BlueZ mesh, so code written against the `org.bluez.mesh` D-Bus API should carry over.
- The node side is specs 10 and 11. The node is provisioned over **PB-ADV**.
- Later this tool will also push configs over a vendor model (future spec, out of scope).

## Hardware details
- A Linux PC with a Bluetooth adapter. `bluetooth-meshd` needs exclusive control of its adapter. Either:
  - use a dedicated USB Bluetooth dongle, or
  - stop `bluetooth.service` while `bluetooth-meshd` runs.
- BlueZ version: use a recent release with the `org.bluez.mesh` D-Bus API (see `doc/mesh-api.txt` in BlueZ). Record the tested version in the tool's README.

## Scope
- In scope:
  - Python package `tools/gateway/` using a D-Bus library (`dbus-next` recommended, asyncio). BlueZ's `test/test-mesh` and `tools/mesh-cfgclient` are reference implementations.
  - Gateway "application" registered with `bluetooth-meshd`. It has one element with these client models:
    - Config Client (0x0001)
    - Generic OnOff Client (0x1001)
    - Sensor Client (0x1102)
    - Health Client (0x0003)
  - Local state file `~/.config/stone-ocean-gateway/state.json`: network token, nodes (UUID, unicast, element count, device ids if known).
  - Subcommands:

    | Command | Action |
    |---|---|
    | `init` | Create the mesh network (CreateNetwork) and app key 0 on net key 0. |
    | `scan [--timeout 10]` | List unprovisioned device UUIDs. |
    | `provision <uuid>` | Provision, then Config: get Composition Data, add app key, bind it to every Sensor/OnOff server model, and set publication of all of them to group `0xC000` (configurable). Save the node. |
    | `nodes` | List known nodes with their elements and models. |
    | `get <node> <element>` | Sensor Get or OnOff Get, chosen by the model on that element. Prints decoded values. |
    | `set <node> <element> on\|off` | Generic OnOff Set (acknowledged). Prints the returned Status. |
    | `listen` | Subscribe to group `0xC000` and print decoded Sensor Status / OnOff Status as they arrive, with timestamps. |
    | `remove <node>` | Config Node Reset, then drop the node from state. |
  - Value decoding for the property IDs and encodings in spec 11, kept in one module shared with tests.
  - `tools/gateway/README.md`: daemon setup, permissions (D-Bus policy), and a typical session.
- Out of scope: vendor config transfer, a web UI, MQTT/cloud bridging (future gateway work).

## Interface
```
python -m gateway init
python -m gateway scan
python -m gateway provision <uuid> [--group 0xC000]
python -m gateway get 0x0002 1
python -m gateway set 0x0004 3 on
python -m gateway listen
```
Python module boundaries: `mesh_app.py` (D-Bus application/element objects), `config_client.py`, `models.py` (Sensor/OnOff message build/parse), `props.py` (property decoding), `state.py`, `cli.py`.

## Behavior & constraints
- All D-Bus calls have timeouts. Retry config messages up to 3 times.
- Unknown property IDs are printed as raw hex instead of raising an error.
- The tool must be usable unattended, for the system tests in spec 13: non-interactive, exit code 0 on success, and `--json` output on `get`, `nodes` and `listen`.

## Verification
- **Simulation/host:** pytest for `models.py` / `props.py` encode/decode, using byte vectors identical to the firmware's host tests (spec 11), and for `state.py`. D-Bus is not needed for these.
- **On hardware:**
  - Run `init`, then `scan` (the node's UUID shows up).
  - `provision` succeeds.
  - `nodes` shows 1 + N elements.
  - `get` on the SHTC3 element prints plausible values.
  - `set` toggles the LED.
  - `listen` shows motion within 1 s and periodic SHTC3 reports.

## Acceptance criteria
- [ ] `init`, `scan`, `provision`, `nodes`, `get`, `set`, `listen` and `remove` work against a real node.
- [ ] Provisioning automatically binds app keys and sets publication on all server models.
- [ ] Decoded values match the readings in the node's log.
- [ ] `--json` output is available for scripting, and exit codes are meaningful.
- [ ] Encode/decode logic is covered by pytest with vectors shared with the firmware tests.
- [ ] README documents BlueZ version, daemon setup and adapter exclusivity.

## Open questions
- The BlueZ version on the developer PC, and whether a dedicated USB adapter is available.
