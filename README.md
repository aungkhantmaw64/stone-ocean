# ESP32 BLE Mesh IoT Node

Config-driven BLE mesh node firmware for the ESP32-DevKitC. A JSON config
stored in flash says which sensors and actuators are attached. See
[`specs/README.md`](specs/README.md) for requirements, architecture and the
implementation plan.

## Toolchain

- **ESP-IDF v5.4.4** (pinned; ESP-BLE-MESH APIs differ between versions)
- Docker image `espressif/idf:v5.4.4`, if ESP-IDF is not installed locally

## Build flavours

| Flavour | Purpose | Build directory |
|---|---|---|
| `hw` | Full firmware with BLE mesh, flashed to a DevKitC | `firmware/build/` |
| `qemu` | Same firmware with Bluetooth and mesh disabled, for Espressif QEMU | `firmware/build-qemu/` |

The boot banner shows the firmware version (`git describe`), the IDF version
and the flavour:

```
I (1392) app_main: mesh_node cd7a159 | IDF v5.4.4 | flavour qemu
```

## Building and running

With ESP-IDF installed and `export.sh` sourced, run from the repository root:

```sh
scripts/build.sh                          # hw build
scripts/build.sh qemu                     # qemu build
scripts/qemu.sh                           # build qemu flavour, boot it in QEMU with the monitor
PORT=/dev/ttyUSB0 scripts/build.sh hw flash monitor
```

`scripts/build.sh [hw|qemu] [idf.py actions...]` passes any actions through to
`idf.py` (default `build`). `PORT` selects the serial port. Quit the monitor
with Ctrl+].

### Using Docker instead of a local ESP-IDF

Run from the repository root. Build output stays owned by your user.

Hardware build:

```sh
docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/project -w /project \
  espressif/idf:v5.4.4 scripts/build.sh
```

Boot the QEMU build:

```sh
docker run --rm -it -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/project -w /project \
  espressif/idf:v5.4.4 scripts/qemu.sh
```

Flash and monitor a DevKitC (find the port with `ls /dev/ttyUSB*`):

```sh
PORT=/dev/ttyUSB0
docker run --rm -it -u $(id -u):$(id -g) -e HOME=/tmp -e PORT=$PORT \
  --device=$PORT --group-add $(stat -c %g $PORT) \
  -v $PWD:/project -w /project espressif/idf:v5.4.4 scripts/build.sh hw flash monitor
```

`--group-add` gives the container user access to the serial port.
