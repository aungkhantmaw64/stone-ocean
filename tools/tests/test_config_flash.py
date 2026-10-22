"""Tests for tools/config_flash.py, the PC tool that checks and writes node configs."""

import json
import shutil
from pathlib import Path

import pytest

import config_flash
from config_flash import EXIT_INVALID, EXIT_OK

REPO_DIR = Path(__file__).resolve().parents[2]
EXAMPLE_CONFIG = REPO_DIR / "configs/example-shtc3-pir-led.json"
CONFIG_LIMIT = 4096  # NODE_CONFIG_MAX_RAW_LEN

# Firmware messages the fake checker prints, as node_config_parse() words them.
DUPLICATE_GPIO_MESSAGE = "devices[1].gpio: GPIO 25 is already used by devices[0].gpio"
FLASH_PIN_MESSAGE = "devices[0].gpio: GPIO 7 is used by the flash"
STRAPPING_PIN_WARNING = "devices[0].gpio: GPIO 2 is a strapping pin and may affect boot"


def led_config(gpio: int, **extra) -> dict:
    """Return a config with one LED on gpio, plus any extra LED keys."""
    return {"version": 1, "node_name": "test-node",
            "devices": [{"id": "led1", "type": "led", "gpio": gpio, "active_high": True,
                         **extra}]}


def write_json(directory: Path, document) -> Path:
    """Write document to a config file in directory and return its path."""
    path = directory / "config.json"
    path.write_text(json.dumps(document))
    return path


def fake_checker(directory: Path, exit_code: int, *stderr_lines: str) -> Path:
    """Return a stand-in for node_config_check that prints stderr_lines and exits."""
    script = directory / "fake_node_config_check"
    printed = "".join(f"printf '%s\\n' '{line}' >&2\n" for line in stderr_lines)
    script.write_text(f"#!/bin/sh\n{printed}exit {exit_code}\n")
    script.chmod(0o755)
    return script


def run_check(config: Path, checker: Path) -> int:
    """Run `config_flash.py --checker CHECKER check CONFIG` and return its exit code."""
    return config_flash.main(["--checker", str(checker), "check", str(config)])


@pytest.fixture(scope="session")
def real_checker() -> Path:
    """The real node_config_check, built with cmake on first use."""
    if shutil.which("cmake") is None:
        pytest.skip("cmake is not installed, so the firmware's config checker cannot be built")
    return config_flash.build_checker()


class TestCheckingAConfigBeforeItIsWritten:
    # AC: specs/05-config-flash-tool.md #1
    def test_example_config_passes_the_check(self, real_checker, capsys):
        exit_code = run_check(EXAMPLE_CONFIG, real_checker)

        assert exit_code == EXIT_OK
        assert "valid" in capsys.readouterr().out

    # AC: specs/05-config-flash-tool.md #1
    def test_config_breaking_a_schema_rule_is_rejected_with_its_path(self, tmp_path, capsys):
        config = write_json(tmp_path, led_config(25, colour="red"))  # the schema allows no unknown keys

        exit_code = run_check(config, fake_checker(tmp_path, 0))

        assert exit_code == EXIT_INVALID
        assert "devices[0]: Additional properties are not allowed" in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #1
    def test_config_breaking_a_firmware_only_rule_is_rejected_with_the_firmware_message(
            self, tmp_path, capsys):
        document = led_config(25)
        document["devices"].append(dict(document["devices"][0], id="led2"))  # same GPIO twice
        config = write_json(tmp_path, document)

        exit_code = run_check(config, fake_checker(tmp_path, 1, DUPLICATE_GPIO_MESSAGE))

        assert exit_code == EXIT_INVALID
        assert DUPLICATE_GPIO_MESSAGE in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #1
    def test_firmware_message_is_shown_when_both_the_schema_and_the_firmware_reject_a_config(
            self, tmp_path, capsys):
        config = write_json(tmp_path, led_config(7))  # GPIO 7 is wired to the flash chip

        exit_code = run_check(config, fake_checker(tmp_path, 1, FLASH_PIN_MESSAGE))

        assert exit_code == EXIT_INVALID
        assert capsys.readouterr().err.strip() == f"invalid config: {FLASH_PIN_MESSAGE}"

    # AC: specs/05-config-flash-tool.md #1
    def test_firmware_warnings_are_shown_but_do_not_fail_the_check(self, tmp_path, capsys):
        config = write_json(tmp_path, led_config(2))
        checker = fake_checker(tmp_path, 0, f"warning: {STRAPPING_PIN_WARNING}")

        exit_code = run_check(config, checker)

        assert exit_code == EXIT_OK
        assert f"warning: {STRAPPING_PIN_WARNING}" in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #1
    def test_malformed_json_is_rejected(self, tmp_path, capsys):
        config = tmp_path / "config.json"
        config.write_text('{"version": 1,')

        exit_code = run_check(config, fake_checker(tmp_path, 0))

        assert exit_code == EXIT_INVALID
        assert "malformed JSON" in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #1
    def test_empty_config_is_rejected(self, tmp_path, capsys):
        config = tmp_path / "config.json"
        config.write_bytes(b"")

        exit_code = run_check(config, fake_checker(tmp_path, 0))

        assert exit_code == EXIT_INVALID
        assert "config is empty" in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #1
    def test_config_larger_than_4_kb_is_rejected(self, tmp_path, capsys):
        config = tmp_path / "config.json"
        config.write_bytes(b" " * (CONFIG_LIMIT + 1))

        exit_code = run_check(config, fake_checker(tmp_path, 0))

        assert exit_code == EXIT_INVALID
        assert f"at most {CONFIG_LIMIT} allowed" in capsys.readouterr().err
