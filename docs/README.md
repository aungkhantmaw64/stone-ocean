# Stone Ocean system docs

Welcome! These pages explain how the Stone Ocean BLE mesh node works as a
*system*: what the pieces are, how they talk to each other, and why things are
built the way they are. They stay out of the code on purpose. If you want the
exact interfaces, the specs in [`specs/`](../specs/README.md) and the headers
are the place to go.

If you only have five minutes, read the overview. If you're about to change the
firmware, read the overview and the active objects page, because that page
explains the one rule that shapes everything else.

## Reading order

| # | Page | What you'll get out of it |
|---|---|---|
| 1 | [System overview](01-system-overview.md) | The big picture: node, PC tools, gateway, and what the system is for |
| 2 | [Firmware architecture](02-firmware-architecture.md) | The layers and components inside the node, and who depends on whom |
| 3 | [Active objects](03-active-objects.md) | How concurrency works: event queues, run to completion, publish/subscribe |
| 4 | [Boot and modes](04-boot-and-modes.md) | What happens from power on, and the difference between normal and safe mode |
| 5 | [Configuration](05-configuration.md) | The JSON config, how it gets onto the node, and how it's checked |
| 6 | [Devices](06-devices.md) | How sensors and actuators are driven, faulted, retried and reported |
| 7 | [Mesh networking](07-mesh.md) | Provisioning, the single vendor model, and the protobuf messages |
| 8 | [Health and recovery](08-health-and-recovery.md) | Watchdog, crash counting, factory reset and the status LED |
| 9 | [Testing strategy](09-testing-strategy.md) | Host tests, QEMU, real hardware, and why each exists |
| 10 | [Glossary](10-glossary.md) | Short definitions of the terms used everywhere else |

Other docs in this folder:

- [Unit testing standards](unit-testing-standards.md): how tests are named and
  written.

## A note on status

The system is being built in numbered chunks (the specs). At the time of
writing, everything on the node is in place, the mesh vendor model is being
verified on hardware, and the PC gateway script (spec 12) and system tests
(spec 13) are still to come. Wherever a page talks about something that isn't
built yet, it says so.
