#!/usr/bin/env python3
"""Build a QEMU flash image, optionally with a node config pre-loaded.

The config is packed exactly as the firmware's config_store writes it
(config_format.py, specs/04-config-storage.md) and stored as config.bin on the
LittleFS `config` partition.

Run after building the QEMU flavour, with the packages in
tools/requirements.txt installed (scripts/qemu.sh does this in a venv):

    tools/config_image.py --build-dir firmware/build-qemu \
        --output firmware/build-qemu/qemu_flash_persistent.bin \
        [--config configs/example-shtc3-pir-led.json]

Without --config the config partition is left erased, as on a new board.
"""

import argparse
import subprocess
import sys
from pathlib import Path

from config_partition import (LITTLEFS_NAME_MAX, build_config_image,
                              find_config_partition)


def read_sdkconfig(build_dir: Path) -> dict:
    """Return the CONFIG_* values of the build's sdkconfig, unquoted."""
    values = {}
    for line in (build_dir / "sdkconfig").read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip('"')
    return values


def write_config_image(build_dir: Path, config: Path, sdkconfig: dict) -> tuple:
    """Write the config partition image for config; return (offset, image path)."""
    name_max = int(sdkconfig.get("CONFIG_LITTLEFS_OBJ_NAME_LEN", str(LITTLEFS_NAME_MAX)))
    if name_max != LITTLEFS_NAME_MAX:
        raise SystemExit(f"CONFIG_LITTLEFS_OBJ_NAME_LEN is {name_max}, but the config tools "
                         f"build images with {LITTLEFS_NAME_MAX}; update config_partition.py")
    table = (build_dir / "partition_table/partition-table.bin").read_bytes()
    partition = find_config_partition(table)
    image = build_dir / "config_preload.bin"
    image.write_bytes(build_config_image(config.read_bytes(), partition.size))
    return partition.offset, image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="built QEMU flavour, e.g. firmware/build-qemu")
    parser.add_argument("--output", type=Path, required=True, help="flash image to write")
    parser.add_argument("--config", type=Path, help="JSON config to pre-load")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    sdkconfig = read_sdkconfig(build_dir)
    extra_images = []
    if args.config is not None:
        offset, image = write_config_image(build_dir, args.config, sdkconfig)
        extra_images = [hex(offset), str(image)]

    subprocess.run([sys.executable, "-m", "esptool", "--chip=esp32", "merge_bin",
                    f"--output={args.output.resolve()}",
                    f"--fill-flash-size={sdkconfig['CONFIG_ESPTOOLPY_FLASHSIZE']}",
                    "@flash_args", *extra_images],
                   cwd=build_dir, check=True, stdout=subprocess.DEVNULL)
    preloaded = f" with {args.config} pre-loaded" if args.config else ""
    print(f"QEMU flash image: {args.output}{preloaded}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
