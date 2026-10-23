#!/usr/bin/env python3
"""Check, write, read and erase the node config, and factory-reset the node, from a PC
(specs/05-config-flash-tool.md, specs/09-health-factory-reset.md).

The config is written straight into the node's `config` flash partition, so
the firmware needs no code to receive it. Before anything is written, the
config is checked against the JSON Schema and then against the firmware's own
parser, built for the PC as tools/node_config_check.

    tools/config_flash.py check configs/example-shtc3-pir-led.json
    tools/config_flash.py --port /dev/ttyUSB0 write configs/example-shtc3-pir-led.json
    tools/config_flash.py --port /dev/ttyUSB0 read [--output FILE]
    tools/config_flash.py --port /dev/ttyUSB0 erase
    tools/config_flash.py --port /dev/ttyUSB0 factory-reset
    tools/config_flash.py --flash-image firmware/build-qemu/qemu_flash_persistent.bin write|read|erase|factory-reset

`write` and `erase` touch only the `config` partition. `factory-reset` erases
the `config` and `nvs` partitions: the config, the mesh state and the crash
counter. The firmware's factory-reset hooks do not run; the node boots
unprovisioned in safe mode. Partitions are found in the partition table on
the target itself. With --port, esptool resets the node
into its bootloader for each access and resets it again afterwards.

Exit codes: 0 success, 1 invalid config, 2 usage error, 3 the target could
not be read or written, 4 no config stored or the stored config is corrupt.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator
from jsonschema.exceptions import best_match

from config_format import CONFIG_MAX_RAW_LEN, ConfigFormatError
from config_partition import (PARTITION_TABLE_LEN, PARTITION_TABLE_OFFSET, ConfigNotFoundError,
                              Partition, PartitionTableError, build_config_image,
                              extract_config, find_config_partition, find_nvs_partition)

EXIT_OK = 0
EXIT_INVALID = 1
EXIT_USAGE = 2
EXIT_TARGET = 3
EXIT_NO_CONFIG = 4

REPO_DIR = Path(__file__).resolve().parent.parent
SCHEMA_FILE = REPO_DIR / "configs/node-config.schema.json"
CHECKER_SOURCE_DIR = REPO_DIR / "tools/node_config_check"
CHECKER_BUILD_DIR = REPO_DIR / "build/node_config_check"
CHECKER_WARNING_PREFIX = "warning: "
ESPTOOL_DEFAULT_BAUD = 460800  # what idf.py flash uses
ERASED_BYTE = b"\xFF"


class ConfigInvalidError(Exception):
    """The config would be rejected by the schema or the firmware.

    The message is "<path>: <reason>", or just the reason when the problem
    concerns the whole file.
    """


class ToolError(Exception):
    """The tool cannot run, for example because cmake is missing."""


class UsageError(Exception):
    """The command line is valid for argparse but not for the subcommand."""


class TargetError(Exception):
    """The serial port or flash image could not be read or written."""


class FlashImageTarget:
    """A QEMU flash image file, edited in place. QEMU must not be running on it."""

    def __init__(self, path: Path) -> None:
        self.path = path

    def __str__(self) -> str:
        return str(self.path)

    def read(self, offset: int, size: int) -> bytes:
        self._check_range(offset, size)
        try:
            with self.path.open("rb") as image:
                image.seek(offset)
                return image.read(size)
        except OSError as error:
            raise TargetError(f"cannot read flash image: {error}") from error

    def write(self, offset: int, data: bytes) -> None:
        self._check_range(offset, len(data))
        try:
            with self.path.open("r+b") as image:
                image.seek(offset)
                image.write(data)
        except OSError as error:
            raise TargetError(f"cannot write flash image: {error}") from error

    def erase(self, offset: int, size: int) -> None:
        self.write(offset, ERASED_BYTE * size)

    def _check_range(self, offset: int, size: int) -> None:
        try:
            image_size = self.path.stat().st_size
        except OSError as error:
            raise TargetError(f"cannot open flash image: {error}") from error
        if offset + size > image_size:
            raise TargetError(f"{self.path} is {image_size} bytes, too small to hold "
                              f"0x{offset:X}..0x{offset + size:X}")


class SerialTarget:
    """A node on a serial port, accessed through its ROM bootloader with esptool."""

    def __init__(self, port: str, baud: int, runner=subprocess.run) -> None:
        self.port = port
        self.baud = baud
        self.runner = runner

    def __str__(self) -> str:
        return self.port

    def read(self, offset: int, size: int) -> bytes:
        with tempfile.TemporaryDirectory() as scratch:
            dump = Path(scratch) / "read.bin"
            self._esptool("read_flash", hex(offset), hex(size), str(dump))
            return dump.read_bytes()

    def write(self, offset: int, data: bytes) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            image = Path(scratch) / "write.bin"
            image.write_bytes(data)
            # write_flash verifies the written data by its MD5 by default.
            self._esptool("write_flash", hex(offset), str(image))

    def erase(self, offset: int, size: int) -> None:
        self._esptool("erase_region", hex(offset), hex(size))

    def _esptool(self, *command: str) -> None:
        result = self.runner([sys.executable, "-m", "esptool", "--chip", "esp32",
                              "--port", self.port, "--baud", str(self.baud), *command],
                             capture_output=True, text=True)
        if result.returncode != 0:
            output = (result.stdout + result.stderr).strip().splitlines()
            raise TargetError(f"esptool {command[0]} on {self.port} failed: "
                              f"{output[-1] if output else 'no output'}")


def json_path_text(path) -> str:
    """Render a jsonschema path such as ['devices', 1, 'gpio'] as devices[1].gpio."""
    text = ""
    for part in path:
        if isinstance(part, int):
            text += f"[{part}]"
        else:
            text += f".{part}" if text else str(part)
    return text


def with_path(path: str, reason: str) -> str:
    """Join a JSON path and a reason the way the firmware does."""
    return f"{path}: {reason}" if path else reason


def check_schema(raw_json: bytes) -> None:
    """Raise ConfigInvalidError if raw_json is malformed or breaks the JSON Schema."""
    try:
        document = json.loads(raw_json)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ConfigInvalidError(f"malformed JSON: {error}") from error
    schema = json.loads(SCHEMA_FILE.read_text())
    error = best_match(Draft202012Validator(schema).iter_errors(document))
    if error is not None:
        raise ConfigInvalidError(with_path(json_path_text(error.absolute_path), error.message))


def build_checker() -> Path:
    """Build node_config_check if needed and return its path."""
    if shutil.which("cmake") is None:
        raise ToolError("cmake is needed to build tools/node_config_check; install it, "
                        "or pass --checker with a prebuilt node_config_check")
    for command in (["cmake", "-S", str(CHECKER_SOURCE_DIR), "-B", str(CHECKER_BUILD_DIR)],
                    ["cmake", "--build", str(CHECKER_BUILD_DIR)]):
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise ToolError(f"building node_config_check failed:\n{result.stdout}{result.stderr}")
    return CHECKER_BUILD_DIR / "node_config_check"


def run_checker(checker: Path, config_file: Path) -> list:
    """Run the firmware parser on config_file and return its warnings.

    Raises ConfigInvalidError if the parser rejects the config.
    """
    result = subprocess.run([str(checker), str(config_file)], capture_output=True, text=True)
    lines = result.stderr.splitlines()
    warnings = [line[len(CHECKER_WARNING_PREFIX):] for line in lines
                if line.startswith(CHECKER_WARNING_PREFIX)]
    errors = [line for line in lines if not line.startswith(CHECKER_WARNING_PREFIX)]
    if result.returncode == 1:
        raise ConfigInvalidError(errors[-1] if errors else "rejected by the firmware parser")
    if result.returncode != 0:
        raise ToolError(f"node_config_check failed: {' '.join(errors)}")
    return warnings


def check_config(config_file: Path, checker: Path) -> tuple:
    """Check a config file as the node would; return (raw JSON, warnings).

    Raises ConfigInvalidError for a config the schema or the node would
    reject. When both reject it, the firmware's message is the one raised:
    it names the rule (for example "reserved for flash"), where the schema
    can only say which value failed.
    """
    raw_json = config_file.read_bytes()
    if not raw_json:
        raise ConfigInvalidError("config is empty")
    if len(raw_json) > CONFIG_MAX_RAW_LEN:
        raise ConfigInvalidError(f"config is {len(raw_json)} bytes, "
                                 f"at most {CONFIG_MAX_RAW_LEN} allowed")
    try:
        check_schema(raw_json)
    except ConfigInvalidError:
        run_checker(checker, config_file)
        raise
    return raw_json, run_checker(checker, config_file)


def make_target(args: argparse.Namespace):
    """Return the target named by --port or --flash-image."""
    if args.port is not None:
        return SerialTarget(args.port, args.baud)
    if args.flash_image is not None:
        return FlashImageTarget(args.flash_image)
    raise UsageError(f"{args.command} needs --port or --flash-image")


def partition_of(target, find) -> Partition:
    """Return the partition that find() picks from the partition table on target."""
    table = target.read(PARTITION_TABLE_OFFSET, PARTITION_TABLE_LEN)
    try:
        return find(table)
    except PartitionTableError as error:
        raise TargetError(f"{target}: {error}") from error


def config_partition_of(target) -> Partition:
    """Return the config partition from the partition table on target."""
    return partition_of(target, find_config_partition)


def command_check(args: argparse.Namespace) -> int:
    """`check FILE`: validate the config without touching a target."""
    _, warnings = check_config(args.file, args.checker or build_checker())
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    print(f"{args.file}: valid")
    return EXIT_OK


def command_write(args: argparse.Namespace) -> int:
    """`write FILE`: check the config, then write it over the config partition."""
    target = make_target(args)
    raw_json, warnings = check_config(args.file, args.checker or build_checker())
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    partition = config_partition_of(target)
    target.write(partition.offset, build_config_image(raw_json, partition.size))
    print(f"{args.file}: written to {target} at 0x{partition.offset:X}")
    return EXIT_OK


def command_read(args: argparse.Namespace) -> int:
    """`read`: print the stored config exactly as it was written."""
    target = make_target(args)
    partition = config_partition_of(target)
    try:
        raw_json = extract_config(target.read(partition.offset, partition.size))
    except (ConfigNotFoundError, ConfigFormatError) as error:
        print(f"{target}: {error}", file=sys.stderr)
        return EXIT_NO_CONFIG
    if args.output is not None:
        args.output.write_bytes(raw_json)
    else:
        sys.stdout.buffer.write(raw_json)
        sys.stdout.buffer.flush()
    return EXIT_OK


def command_erase(args: argparse.Namespace) -> int:
    """`erase`: erase the config partition; the node then boots in safe mode."""
    target = make_target(args)
    partition = config_partition_of(target)
    target.erase(partition.offset, partition.size)
    print(f"config erased on {target}")
    return EXIT_OK


def command_factory_reset(args: argparse.Namespace) -> int:
    """`factory-reset`: erase the config and nvs partitions; the node boots unprovisioned."""
    target = make_target(args)
    config = config_partition_of(target)
    nvs = partition_of(target, find_nvs_partition)
    target.erase(config.offset, config.size)
    target.erase(nvs.offset, nvs.size)
    print(f"factory reset on {target}: config and nvs erased")
    return EXIT_OK


def parse_args(argv: list) -> argparse.Namespace:
    """Parse the command line; argparse exits with EXIT_USAGE on a bad one."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    target = parser.add_mutually_exclusive_group()
    target.add_argument("--port", help="serial port of the node, e.g. /dev/ttyUSB0")
    target.add_argument("--flash-image", type=Path, help="QEMU flash image file")
    parser.add_argument("--baud", type=int, default=ESPTOOL_DEFAULT_BAUD,
                        help=f"esptool baud rate (default {ESPTOOL_DEFAULT_BAUD})")
    parser.add_argument("--checker", type=Path,
                        help="prebuilt node_config_check (default: build it with cmake)")
    commands = parser.add_subparsers(dest="command", required=True)

    check = commands.add_parser("check", help="validate a config file")
    check.add_argument("file", type=Path, help="JSON config")
    check.set_defaults(run=command_check)

    write = commands.add_parser("write", help="check a config and write it to the target")
    write.add_argument("file", type=Path, help="JSON config")
    write.set_defaults(run=command_write)

    read = commands.add_parser("read", help="print the config stored on the target")
    read.add_argument("--output", type=Path, help="write the config here instead of stdout")
    read.set_defaults(run=command_read)

    erase = commands.add_parser("erase", help="erase the config stored on the target")
    erase.set_defaults(run=command_erase)

    factory_reset = commands.add_parser(
        "factory-reset", help="erase the config, the mesh state and the crash counter")
    factory_reset.set_defaults(run=command_factory_reset)

    return parser.parse_args(argv)


def main(argv: list) -> int:
    args = parse_args(argv)
    try:
        return args.run(args)
    except ConfigInvalidError as error:
        print(f"invalid config: {error}", file=sys.stderr)
        return EXIT_INVALID
    except TargetError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_TARGET
    except (OSError, ToolError, UsageError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
