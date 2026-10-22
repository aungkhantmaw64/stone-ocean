#!/usr/bin/env python3
"""Check, write, read and erase the node config from a PC (specs/05-config-flash-tool.md).

The config is written straight into the node's `config` flash partition, so
the firmware needs no code to receive it. Before anything is written, the
config is checked against the JSON Schema and then against the firmware's own
parser, built for the PC as tools/node_config_check.

    tools/config_flash.py check configs/example-shtc3-pir-led.json

Exit codes: 0 success, 1 invalid config, 2 usage error, 3 the target could
not be read or written, 4 no config stored or the stored config is corrupt.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

from jsonschema import Draft202012Validator
from jsonschema.exceptions import best_match

from config_format import CONFIG_MAX_RAW_LEN

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


class ConfigInvalidError(Exception):
    """The config would be rejected by the schema or the firmware.

    The message is "<path>: <reason>", or just the reason when the problem
    concerns the whole file.
    """


class ToolError(Exception):
    """The tool cannot run, for example because cmake is missing."""


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


def command_check(args: argparse.Namespace) -> int:
    """`check FILE`: validate the config without touching a target."""
    _, warnings = check_config(args.file, args.checker or build_checker())
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    print(f"{args.file}: valid")
    return EXIT_OK


def parse_args(argv: list) -> argparse.Namespace:
    """Parse the command line; argparse exits with EXIT_USAGE on a bad one."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--checker", type=Path,
                        help="prebuilt node_config_check (default: build it with cmake)")
    commands = parser.add_subparsers(dest="command", required=True)

    check = commands.add_parser("check", help="validate a config file")
    check.add_argument("file", type=Path, help="JSON config")
    check.set_defaults(run=command_check)

    return parser.parse_args(argv)


def main(argv: list) -> int:
    args = parse_args(argv)
    try:
        return args.run(args)
    except ConfigInvalidError as error:
        print(f"invalid config: {error}", file=sys.stderr)
        return EXIT_INVALID
    except (OSError, ToolError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
