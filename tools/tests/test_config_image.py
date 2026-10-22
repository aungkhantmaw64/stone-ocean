"""Tests for tools/config_image.py, which packs configs for QEMU flash images."""

import struct

import pytest

from config_format import pack_config

HEADER_FORMAT = "<IIII"  # magic, format version, JSON length, CRC32; little-endian
HEADER_LEN = struct.calcsize(HEADER_FORMAT)
FIRMWARE_MAGIC = 0x4746434E  # "NCFG" read as a little-endian uint32 (config_store.c)
FIRMWARE_FORMAT_VERSION = 1  # CONFIG_STORE_FORMAT_VERSION
CONFIG_LIMIT = 4096  # NODE_CONFIG_MAX_RAW_LEN

SMALL_CONFIG = b'{"version":1}'
# CRC32 (IEEE 802.3) of SMALL_CONFIG; firmware/test/host/test_config_store_crc.c
# checks that the firmware's CRC32 is the same algorithm.
SMALL_CONFIG_CRC32 = 0x71309A33


def json_string_of_length(length: int) -> bytes:
    """Return a valid JSON document (one string) exactly length bytes long."""
    return b'"' + b"x" * (length - 2) + b'"'


class TestPackingAConfigForQemu:
    def test_packed_config_starts_with_the_header_the_firmware_expects(self):
        packed = pack_config(SMALL_CONFIG)

        magic, version, length, _ = struct.unpack(HEADER_FORMAT, packed[:HEADER_LEN])
        assert packed[:4] == b"NCFG"
        assert magic == FIRMWARE_MAGIC
        assert version == FIRMWARE_FORMAT_VERSION
        assert length == len(SMALL_CONFIG)
        assert packed[HEADER_LEN:] == SMALL_CONFIG

    def test_packed_config_checksum_matches_the_json(self):
        packed = pack_config(SMALL_CONFIG)

        _, _, _, crc = struct.unpack(HEADER_FORMAT, packed[:HEADER_LEN])
        assert crc == SMALL_CONFIG_CRC32

    def test_config_of_exactly_4_kb_is_packed(self):
        raw = json_string_of_length(CONFIG_LIMIT)

        packed = pack_config(raw)

        assert len(packed) == HEADER_LEN + CONFIG_LIMIT


class TestRefusingConfigsTheFirmwareWouldReject:
    def test_config_larger_than_4_kb_is_refused(self):
        with pytest.raises(ValueError, match="limit"):
            pack_config(json_string_of_length(CONFIG_LIMIT + 1))

    def test_empty_config_is_refused(self):
        with pytest.raises(ValueError, match="empty"):
            pack_config(b"")

    def test_malformed_json_is_refused(self):
        with pytest.raises(ValueError):
            pack_config(b'{"version": 1,')
