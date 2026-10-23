"""Tests for tools/config_flash.py, the PC tool that checks and writes node configs."""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

import config_flash
from config_flash import (EXIT_INVALID, EXIT_NO_CONFIG, EXIT_OK, EXIT_TARGET, EXIT_USAGE,
                          SerialTarget)
from config_format import pack_config
from config_partition import extract_config, littlefs_for
from flash_layout import (CONFIG_OFFSET, CONFIG_SIZE, TABLE_LEN, TABLE_OFFSET, node_flash_image,
                          partition_table)

REPO_DIR = Path(__file__).resolve().parents[2]
EXAMPLE_CONFIG = REPO_DIR / "configs/example-shtc3-pir-led.json"
CONFIG_LIMIT = 4096  # NODE_CONFIG_MAX_RAW_LEN

# Firmware messages the fake checker prints, as node_config_parse() words them.
DUPLICATE_GPIO_MESSAGE = "devices[1].gpio: GPIO 25 is already used by devices[0].gpio"
FLASH_PIN_MESSAGE = "devices[0].gpio: GPIO 7 is used by the flash"
STATUS_LED_PIN = 0  # NODE_HAL_BOARD_STATUS_LED_GPIO
RESET_BUTTON_PIN = 34  # NODE_HAL_BOARD_RESET_BUTTON_GPIO
STRAPPING_PIN_WARNING = "devices[0].gpio: GPIO 2 is a strapping pin and may affect boot"


def led_config(gpio: int, **extra) -> dict:
    """Return a config with one LED on gpio, plus any extra LED keys."""
    return {"version": 1, "node_name": "test-node",
            "devices": [{"id": "led1", "type": "led", "gpio": gpio, "active_high": True,
                         **extra}]}


def pir_config(gpio: int) -> dict:
    """Return a config with one motion sensor on gpio and no pull resistor."""
    return {"version": 1, "node_name": "test-node",
            "devices": [{"id": "pir1", "type": "pir", "gpio": gpio, "active_high": True}]}


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


def config_partition_of(image_file: Path) -> bytes:
    """Return the config partition's bytes from a flash image file."""
    return image_file.read_bytes()[CONFIG_OFFSET:CONFIG_OFFSET + CONFIG_SIZE]


def outside_config_partition(image_file: Path) -> bytes:
    """Return every byte of a flash image file except the config partition."""
    image = image_file.read_bytes()
    return image[:CONFIG_OFFSET] + image[CONFIG_OFFSET + CONFIG_SIZE:]


def store_config_file(image_file: Path, contents: bytes) -> None:
    """Put a config partition whose config.bin holds exactly contents into a flash image file."""
    fs = littlefs_for(CONFIG_SIZE)
    fs.format()
    fs.mount()
    with fs.open("config.bin", "wb") as config_file:
        config_file.write(contents)
    fs.unmount()
    image = bytearray(image_file.read_bytes())
    image[CONFIG_OFFSET:CONFIG_OFFSET + CONFIG_SIZE] = fs.context.buffer
    image_file.write_bytes(image)


class FakeEsptool:
    """Stands in for `python -m esptool`: records each command and plays a node.

    read_flash returns the node's partition table; write_flash records how many
    bytes were written. A failing fake answers every command as esptool does
    when no node is connected.
    """

    def __init__(self, fails=False) -> None:
        self.fails = fails
        self.commands = []  # [esptool command, its arguments...]
        self.written_sizes = []

    def __call__(self, argv, **_):
        command = argv[argv.index("--baud") + 2:]
        self.commands.append(command)
        if self.fails:
            return subprocess.CompletedProcess(argv, 2, "", "A fatal error occurred: "
                                               "Could not open /dev/ttyUSB0")
        if command[0] == "read_flash":
            Path(command[3]).write_bytes(partition_table())
        if command[0] == "write_flash":
            self.written_sizes.append(Path(command[2]).stat().st_size)
        return subprocess.CompletedProcess(argv, 0, "", "")


@pytest.fixture
def flash_image(tmp_path) -> Path:
    """A QEMU-style flash image of the node with no config stored and mesh state in nvs."""
    image_file = tmp_path / "flash.bin"
    image_file.write_bytes(node_flash_image())
    return image_file


@pytest.fixture
def esptool(monkeypatch) -> FakeEsptool:
    """Route --port targets through a FakeEsptool instead of a real serial port."""
    fake = FakeEsptool()
    monkeypatch.setattr(config_flash, "SerialTarget",
                        lambda port, baud: SerialTarget(port, baud, runner=fake))
    return fake


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

    # AC: specs/16-status-led.md #4
    def test_config_using_the_status_led_pin_is_rejected_by_the_schema(self, tmp_path, capsys):
        config = write_json(tmp_path, led_config(STATUS_LED_PIN))

        exit_code = run_check(config, fake_checker(tmp_path, 0))  # only the schema can reject it

        assert exit_code == EXIT_INVALID
        assert "devices[0].gpio" in capsys.readouterr().err

    # AC: specs/16-status-led.md #4
    def test_config_using_the_reset_button_pin_is_rejected_by_the_schema(self, tmp_path, capsys):
        config = write_json(tmp_path, pir_config(RESET_BUTTON_PIN))

        exit_code = run_check(config, fake_checker(tmp_path, 0))  # only the schema can reject it

        assert exit_code == EXIT_INVALID
        assert "devices[0].gpio" in capsys.readouterr().err

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


class TestWritingAConfig:
    # AC: specs/05-config-flash-tool.md #2
    def test_config_written_to_a_flash_image_is_stored_in_the_config_partition(
            self, tmp_path, flash_image):
        exit_code = config_flash.main(["--checker", str(fake_checker(tmp_path, 0)),
                                       "--flash-image", str(flash_image),
                                       "write", str(EXAMPLE_CONFIG)])

        assert exit_code == EXIT_OK
        assert extract_config(config_partition_of(flash_image)) == EXAMPLE_CONFIG.read_bytes()

    # AC: specs/05-config-flash-tool.md #2
    def test_invalid_config_leaves_the_flash_image_untouched(self, tmp_path, flash_image):
        before = flash_image.read_bytes()
        config = write_json(tmp_path, led_config(7))

        exit_code = config_flash.main(["--checker", str(fake_checker(tmp_path, 1, FLASH_PIN_MESSAGE)),
                                       "--flash-image", str(flash_image), "write", str(config)])

        assert exit_code == EXIT_INVALID
        assert flash_image.read_bytes() == before

    # AC: specs/05-config-flash-tool.md #5
    def test_writing_a_config_leaves_the_rest_of_the_flash_untouched(self, tmp_path, flash_image):
        before = outside_config_partition(flash_image)

        config_flash.main(["--checker", str(fake_checker(tmp_path, 0)),
                           "--flash-image", str(flash_image), "write", str(EXAMPLE_CONFIG)])

        assert outside_config_partition(flash_image) == before

    # AC: specs/05-config-flash-tool.md #2
    def test_write_without_a_target_is_refused_as_a_usage_error(self, tmp_path, capsys):
        exit_code = config_flash.main(["--checker", str(fake_checker(tmp_path, 0)),
                                       "write", str(EXAMPLE_CONFIG)])

        assert exit_code == EXIT_USAGE
        assert "needs --port or --flash-image" in capsys.readouterr().err


class TestReadingTheStoredConfig:
    # AC: specs/05-config-flash-tool.md #3
    def test_read_prints_the_stored_config_exactly_as_written(self, flash_image, capsysbinary):
        store_config_file(flash_image, pack_config(EXAMPLE_CONFIG.read_bytes()))

        exit_code = config_flash.main(["--flash-image", str(flash_image), "read"])

        assert exit_code == EXIT_OK
        assert capsysbinary.readouterr().out == EXAMPLE_CONFIG.read_bytes()

    # AC: specs/05-config-flash-tool.md #3
    def test_read_saves_the_stored_config_to_a_file_when_asked(self, tmp_path, flash_image):
        store_config_file(flash_image, pack_config(EXAMPLE_CONFIG.read_bytes()))
        output = tmp_path / "read-back.json"

        exit_code = config_flash.main(["--flash-image", str(flash_image), "read",
                                       "--output", str(output)])

        assert exit_code == EXIT_OK
        assert output.read_bytes() == EXAMPLE_CONFIG.read_bytes()

    # AC: specs/05-config-flash-tool.md #3
    def test_read_reports_no_config_when_nothing_is_stored(self, flash_image, capsys):
        exit_code = config_flash.main(["--flash-image", str(flash_image), "read"])

        assert exit_code == EXIT_NO_CONFIG
        assert "holds no filesystem" in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #3
    def test_read_reports_a_corrupt_stored_config(self, flash_image, capsys):
        damaged = bytearray(pack_config(EXAMPLE_CONFIG.read_bytes()))
        damaged[-2] ^= 0x01  # flip one bit inside the JSON
        store_config_file(flash_image, bytes(damaged))

        exit_code = config_flash.main(["--flash-image", str(flash_image), "read"])

        assert exit_code == EXIT_NO_CONFIG
        assert "CRC" in capsys.readouterr().err

    # AC: specs/05-config-flash-tool.md #3
    def test_flash_image_too_small_for_the_partition_table_is_reported_as_a_target_error(
            self, tmp_path, capsys):
        too_small = tmp_path / "flash.bin"
        too_small.write_bytes(b"\xFF" * TABLE_OFFSET)  # ends where the partition table would start

        exit_code = config_flash.main(["--flash-image", str(too_small), "read"])

        assert exit_code == EXIT_TARGET
        assert "too small" in capsys.readouterr().err


class TestErasingTheConfig:
    # AC: specs/05-config-flash-tool.md #4
    def test_erased_config_is_no_longer_stored(self, flash_image):
        store_config_file(flash_image, pack_config(EXAMPLE_CONFIG.read_bytes()))

        exit_code = config_flash.main(["--flash-image", str(flash_image), "erase"])

        assert exit_code == EXIT_OK
        assert config_partition_of(flash_image) == b"\xFF" * CONFIG_SIZE

    # AC: specs/05-config-flash-tool.md #5
    def test_erasing_leaves_the_rest_of_the_flash_untouched(self, flash_image):
        store_config_file(flash_image, pack_config(EXAMPLE_CONFIG.read_bytes()))
        before = outside_config_partition(flash_image)

        config_flash.main(["--flash-image", str(flash_image), "erase"])

        assert outside_config_partition(flash_image) == before


class TestTalkingToANodeOverSerial:
    # AC: specs/05-config-flash-tool.md #5
    def test_write_over_serial_writes_only_the_config_partition(self, tmp_path, esptool):
        exit_code = config_flash.main(["--checker", str(fake_checker(tmp_path, 0)),
                                       "--port", "/dev/ttyUSB0", "write", str(EXAMPLE_CONFIG)])

        assert exit_code == EXIT_OK
        read_table, write_config = esptool.commands
        assert read_table[:3] == ["read_flash", hex(TABLE_OFFSET), hex(TABLE_LEN)]
        assert write_config[:2] == ["write_flash", hex(CONFIG_OFFSET)]
        assert esptool.written_sizes == [CONFIG_SIZE]

    # AC: specs/05-config-flash-tool.md #5
    def test_erase_over_serial_erases_only_the_config_partition(self, esptool):
        exit_code = config_flash.main(["--port", "/dev/ttyUSB0", "erase"])

        assert exit_code == EXIT_OK
        assert esptool.commands[-1] == ["erase_region", hex(CONFIG_OFFSET), hex(CONFIG_SIZE)]

    # AC: specs/05-config-flash-tool.md #3
    def test_serial_failure_is_reported_as_a_target_error(self, esptool, capsys):
        esptool.fails = True

        exit_code = config_flash.main(["--port", "/dev/ttyUSB0", "read"])

        assert exit_code == EXIT_TARGET
        assert "Could not open /dev/ttyUSB0" in capsys.readouterr().err
