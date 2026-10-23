"""Tests for tools/config_partition.py: finding the config and nvs partitions, and config
partition images."""

import pytest

from config_format import ConfigFormatError, pack_config
from config_partition import (ConfigNotFoundError, PartitionTableError, build_config_image,
                              extract_config, find_config_partition, find_nvs_partition,
                              littlefs_for)
from flash_layout import (CONFIG_OFFSET, CONFIG_SIZE, DATA_TYPE, LITTLEFS_SUBTYPE,
                          NODE_PARTITIONS, NVS_OFFSET, NVS_SIZE, NVS_SUBTYPE, TABLE_LEN,
                          partition_table)

SMALL_CONFIG = b'{"version":1}'
CONFIG_FILE = "config.bin"


def partitions_with_config(ptype=DATA_TYPE, size=CONFIG_SIZE) -> list:
    """Return the node's partitions with the config partition's type or size changed."""
    others = [row for row in NODE_PARTITIONS if row[0] != "config"]
    return others + [("config", ptype, LITTLEFS_SUBTYPE, CONFIG_OFFSET, size)]


def partitions_with_nvs(subtype=NVS_SUBTYPE, offset=NVS_OFFSET) -> list:
    """Return the node's partitions with the nvs partition's subtype or offset changed."""
    others = [row for row in NODE_PARTITIONS if row[0] != "nvs"]
    return [("nvs", DATA_TYPE, subtype, offset, NVS_SIZE)] + others


def image_with_config_file(contents: bytes) -> bytes:
    """Return a config partition image whose config.bin holds exactly contents."""
    fs = littlefs_for(CONFIG_SIZE)
    fs.format()
    fs.mount()
    with fs.open(CONFIG_FILE, "wb") as config_file:
        config_file.write(contents)
    fs.unmount()
    return bytes(fs.context.buffer)


class TestFindingTheConfigPartition:
    # AC: specs/05-config-flash-tool.md #5
    def test_config_partition_is_found_in_the_node_partition_table(self):
        partition = find_config_partition(partition_table())

        assert partition.name == "config"
        assert partition.offset == CONFIG_OFFSET
        assert partition.size == CONFIG_SIZE

    # AC: specs/05-config-flash-tool.md #5
    def test_partition_table_without_a_config_partition_is_refused(self):
        table = partition_table([row for row in NODE_PARTITIONS if row[0] != "config"])

        with pytest.raises(PartitionTableError, match="no 'config' partition"):
            find_config_partition(table)

    # AC: specs/05-config-flash-tool.md #5
    def test_partition_table_with_a_corrupted_byte_is_refused(self):
        table = bytearray(partition_table())
        table[40] ^= 0x01  # inside the second entry's size field

        with pytest.raises(PartitionTableError, match="MD5"):
            find_config_partition(bytes(table))

    # AC: specs/05-config-flash-tool.md #5
    def test_partition_table_without_an_end_marker_is_refused(self):
        entry = partition_table()[:32]
        table = entry * (TABLE_LEN // len(entry))  # entries all the way, no 0xFF end entry

        with pytest.raises(PartitionTableError, match="end marker"):
            find_config_partition(table)

    # AC: specs/05-config-flash-tool.md #5
    def test_config_partition_that_is_not_a_data_partition_is_refused(self):
        table = partition_table(partitions_with_config(ptype=0x00))

        with pytest.raises(PartitionTableError, match="not a data partition"):
            find_config_partition(table)

    # AC: specs/05-config-flash-tool.md #5
    def test_config_partition_that_is_not_a_whole_number_of_4_kb_blocks_is_refused(self):
        table = partition_table(partitions_with_config(size=CONFIG_SIZE + 512))

        with pytest.raises(PartitionTableError, match="multiple of 4096"):
            find_config_partition(table)


class TestFindingTheNvsPartition:
    # AC: specs/09-health-factory-reset.md #4
    def test_nvs_partition_is_found_in_the_partition_table(self):
        partition = find_nvs_partition(partition_table())

        assert partition.name == "nvs"
        assert partition.offset == NVS_OFFSET
        assert partition.size == NVS_SIZE

    # AC: specs/09-health-factory-reset.md #4
    def test_nvs_partition_that_is_not_aligned_to_flash_sectors_is_rejected(self):
        table = partition_table(partitions_with_nvs(offset=NVS_OFFSET + 512))

        with pytest.raises(PartitionTableError, match="not aligned"):
            find_nvs_partition(table)

    # AC: specs/09-health-factory-reset.md #4
    def test_partition_named_nvs_that_is_not_an_nvs_data_partition_is_rejected(self):
        table = partition_table(partitions_with_nvs(subtype=LITTLEFS_SUBTYPE))

        with pytest.raises(PartitionTableError, match="not an NVS data partition"):
            find_nvs_partition(table)


class TestConfigPartitionImages:
    # AC: specs/05-config-flash-tool.md #3
    def test_config_written_into_a_partition_image_is_read_back_exactly(self):
        image = build_config_image(SMALL_CONFIG, CONFIG_SIZE)

        assert extract_config(image) == SMALL_CONFIG

    # AC: specs/05-config-flash-tool.md #2
    def test_partition_image_fills_the_whole_config_partition(self):
        image = build_config_image(SMALL_CONFIG, CONFIG_SIZE)

        assert len(image) == CONFIG_SIZE

    # AC: specs/05-config-flash-tool.md #4
    def test_erased_partition_is_reported_as_holding_no_config(self):
        erased = b"\xFF" * CONFIG_SIZE

        with pytest.raises(ConfigNotFoundError):
            extract_config(erased)

    # AC: specs/05-config-flash-tool.md #3
    def test_partition_with_a_filesystem_but_no_config_file_is_reported_as_holding_no_config(self):
        fs = littlefs_for(CONFIG_SIZE)
        fs.format()
        empty_filesystem = bytes(fs.context.buffer)

        with pytest.raises(ConfigNotFoundError, match="no config stored"):
            extract_config(empty_filesystem)

    # AC: specs/05-config-flash-tool.md #3
    def test_partition_with_a_damaged_config_file_is_reported_as_corrupt(self):
        damaged = bytearray(pack_config(SMALL_CONFIG))
        damaged[-2] ^= 0x01  # flip one bit inside the JSON
        image = image_with_config_file(bytes(damaged))

        with pytest.raises(ConfigFormatError, match="CRC"):
            extract_config(image)
