#!/usr/bin/env python3
"""Build a QEMU flash image, optionally with a node config pre-loaded.

The config is packed exactly as the firmware's config_store writes it
(config_format.py, specs/04-config-storage.md) and stored as config.bin on the
LittleFS `config` partition.

Run inside the ESP-IDF environment, after building the QEMU flavour:

    tools/config_image.py --build-dir firmware/build-qemu \
        --output firmware/build-qemu/qemu_flash_persistent.bin \
        [--config configs/example-shtc3-pir-led.json]

Without --config the config partition is left erased, as on a new board.
"""

import argparse
import os
import subprocess
import sys
import tempfile
import venv
from pathlib import Path

from config_format import CONFIG_FILE_NAME, pack_config

CONFIG_PARTITION = "config"
LITTLEFS_BLOCK_SIZE = 4096  # what littlefs_create_partition_image() uses

REPO_DIR = Path(__file__).resolve().parent.parent
LITTLEFS_COMPONENT_DIR = REPO_DIR / "firmware/managed_components/joltwallet__littlefs"


def read_sdkconfig(build_dir: Path) -> dict:
    """Return the CONFIG_* values of the build's sdkconfig, unquoted."""
    values = {}
    for line in (build_dir / "sdkconfig").read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip('"')
    return values


def parse_size(text: str) -> int:
    """Parse a partition table size such as 64K, 2M or 0x10000."""
    units = {"K": 1024, "M": 1024 * 1024}
    if text[-1].upper() in units:
        return int(text[:-1], 0) * units[text[-1].upper()]
    return int(text, 0)


def find_partition(build_dir: Path, name: str) -> tuple:
    """Return (offset, size) in bytes of a partition in the built table."""
    gen = Path(os.environ["IDF_PATH"]) / "components/partition_table/gen_esp32part.py"
    table = build_dir / "partition_table/partition-table.bin"
    csv = subprocess.run([sys.executable, str(gen), "--quiet", str(table)], check=True,
                         capture_output=True, text=True).stdout
    for line in csv.splitlines():
        fields = [field.strip() for field in line.split(",")]
        if fields and fields[0] == name:
            return int(fields[3], 0), parse_size(fields[4])
    raise SystemExit(f"partition '{name}' not found in {table}")


def littlefs_python(build_dir: Path) -> Path:
    """Return the littlefs-python CLI, installed in a venv on first use.

    The version is the one the littlefs component pins for its own image
    builds, so the image matches what the firmware mounts.
    """
    venv_dir = build_dir / "config_image_venv"
    cli = venv_dir / "bin/littlefs-python"
    if not cli.exists():
        venv.create(venv_dir, with_pip=True)
        requirements = LITTLEFS_COMPONENT_DIR / "image-building-requirements.txt"
        subprocess.run([str(venv_dir / "bin/pip"), "install", "--quiet", "-r",
                        str(requirements)], check=True)
    return cli


def build_littlefs_image(build_dir: Path, config: Path, size: int, name_max: int) -> Path:
    """Write a LittleFS image holding the packed config and return its path."""
    packed = pack_config(config.read_bytes())
    image = build_dir / "config_preload.bin"
    with tempfile.TemporaryDirectory() as root:
        (Path(root) / CONFIG_FILE_NAME).write_bytes(packed)
        subprocess.run([str(littlefs_python(build_dir)), "create", root, str(image),
                        f"--fs-size={size}", f"--name-max={name_max}",
                        f"--block-size={LITTLEFS_BLOCK_SIZE}"],
                       check=True, stdout=subprocess.DEVNULL)
    return image


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
        offset, size = find_partition(build_dir, CONFIG_PARTITION)
        name_max = int(sdkconfig.get("CONFIG_LITTLEFS_OBJ_NAME_LEN", "64"))
        image = build_littlefs_image(build_dir, args.config, size, name_max)
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
