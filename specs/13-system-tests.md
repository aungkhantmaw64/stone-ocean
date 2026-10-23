# 13 — System tests

**Status:** todo
**Depends on:** 01–12
**Target:** QEMU + 2× ESP32-DevKitC + Linux PC with BlueZ · pytest

## Goal
Prove the full v1 loop works end-to-end, and keep it working. Automated QEMU tests run in CI. A scripted hardware-in-the-loop (HIL) suite runs on the developer's bench.

## Context
Exercises everything:
- config flashing (05) → config-driven boot (04, 06–08)
- provisioning (10, 12) → sensor reads, events and LED control (11, 12)
- relaying through a second node (10)
- safe mode, watchdog and factory reset (04, 09)

## Hardware details
- Bench: 2+ DevKitC boards on USB. Node A has the SHTC3, PIR and LED wired per `configs/example-shtc3-pir-led.json`. Node B has a config with only an LED (or no devices), and acts as relay.
- Linux PC with BlueZ mesh (spec 12).
- The PIR cannot be triggered automatically. PIR tests are marked `manual` and prompt the operator, or are skipped with `-m "not manual"`.

## Scope
- In scope:
  - `tests/system/` pytest suite with markers `qemu`, `hil` and `manual`.
  - Fixtures: QEMU node (boot the QEMU build, serial over `socket://`), HW node (serial port from env `NODE_A_PORT`, `NODE_B_PORT`), and gateway (wraps `python -m gateway ... --json`).
  - **QEMU tests (CI):**
    1. Fresh boot → `SAFE MODE: no config stored`.
    2. `config_flash.py write` with an invalid config → non-zero exit with the JSON path. The flash image is unchanged.
    3. Write the example config into the flash image → boot → the log shows normal mode. On QEMU, devices fault because there's no SHTC3, and the node stays up.
    4. `config_flash.py factory-reset` on the flash image → boot → safe mode again.
    5. `NODE_HEALTH_DEBUG_HANG` build → TWDT reset → `crash_count=1` in the log.
  - **HIL tests:**
    1. Factory-reset A and B, then write their configs with the flashing tool.
    2. Provision A and B via the gateway. Check each node's composition (element 0 with the Stone Ocean Node Server) and that its device list matches its config.
    3. SHTC3 `get` on A matches the reading in A's log within tolerance.
    4. `set` LED on/off on A. The Status matches. A's log shows the state.
    5. `listen` receives a periodic SHTC3 report within `max_interval_ms` + 5 s.
    6. **Relay:** the operator moves A out of the gateway's direct range, or the test lowers the gateway adapter's TX power if supported. Repeat test 4 via B. Document the bench procedure.
    7. Reboot A → still provisioned, `get` works without reprovisioning.
    8. `manual`: wave at the PIR → motion event in `listen` within 1 s.
  - CI job for the `qemu` marker.
  - `tests/system/README.md`: bench wiring, env vars, how to run each marker.
- Out of scope: long-duration soak, RF range characterisation.

## Interface
```
pytest tests/system -m qemu
NODE_A_PORT=/dev/ttyUSB0 NODE_B_PORT=/dev/ttyUSB1 pytest tests/system -m "hil and not manual"
pytest tests/system -m manual
```

## Behavior & constraints
- Tests are independent. Each HIL test starts from a known state via fixtures (factory reset + provision as needed), with session-scoped caching to keep run time reasonable.
- The serial log of every node is captured to `tests/system/logs/<test>.log` on failure.
- Timeouts are explicit and derived from config intervals.

## Verification
- **Simulation/host:** the `qemu` marker passes in CI.
- **On hardware:** the `hil` marker passes on the bench, and `manual` passes with an operator.

## Acceptance criteria
- [ ] All five QEMU tests pass in CI.
- [ ] HIL tests 1–5 and 7 pass on the bench without manual steps.
- [ ] The relay test (6) is documented and passes with the described procedure.
- [ ] The PIR manual test passes.
- [ ] Failure logs from every node are captured automatically.

## Open questions
- How to force multi-hop on a small bench. Distance/shielding vs. reduced TX power vs. a node-side debug option that ignores the gateway's address directly. Decide when the bench is set up.
