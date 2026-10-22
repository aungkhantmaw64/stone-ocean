"""Tests for tools/config_image.py, which pre-loads a config into QEMU flash images."""

from pathlib import Path

import pytest

from config_image import write_config_image
from config_partition import extract_config
from flash_layout import CONFIG_OFFSET, partition_table

REPO_DIR = Path(__file__).resolve().parents[2]
EXAMPLE_CONFIG = REPO_DIR / "configs/example-shtc3-pir-led.json"
FIRMWARE_NAME_LEN = {"CONFIG_LITTLEFS_OBJ_NAME_LEN": "64"}  # the firmware's setting
SHORTER_NAME_LEN = {"CONFIG_LITTLEFS_OBJ_NAME_LEN": "32"}


@pytest.fixture
def build_dir(tmp_path) -> Path:
    """A QEMU build directory holding just the built partition table."""
    (tmp_path / "partition_table").mkdir()
    (tmp_path / "partition_table/partition-table.bin").write_bytes(partition_table())
    return tmp_path


class TestPreloadingAConfigForQemu:
    # AC: specs/05-config-flash-tool.md #2
    def test_qemu_config_image_holds_the_config_at_the_config_partition_offset(self, build_dir):
        offset, image = write_config_image(build_dir, EXAMPLE_CONFIG, FIRMWARE_NAME_LEN)

        assert offset == CONFIG_OFFSET
        assert extract_config(image.read_bytes()) == EXAMPLE_CONFIG.read_bytes()

    # AC: specs/05-config-flash-tool.md #2
    def test_qemu_config_image_is_refused_when_the_firmware_file_name_length_differs(
            self, build_dir):
        with pytest.raises(SystemExit, match="CONFIG_LITTLEFS_OBJ_NAME_LEN is 32"):
            write_config_image(build_dir, EXAMPLE_CONFIG, SHORTER_NAME_LEN)
