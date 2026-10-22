"""Pack and unpack config.bin, the file the firmware's config_store reads.

The layout is defined in specs/04-config-storage.md: a 16-byte header of four
little-endian uint32 fields (magic "NCFG", format version, JSON length, CRC32
of the JSON), then the raw JSON. The checks in unpack_config() are the ones
config_store_load() makes at boot, so a file this module accepts is one the
firmware accepts too.
"""

import json
import struct
import zlib

CONFIG_MAGIC = 0x4746434E  # "NCFG" read as a little-endian uint32
CONFIG_FORMAT_VERSION = 1  # CONFIG_STORE_FORMAT_VERSION
CONFIG_MAX_RAW_LEN = 4096  # NODE_CONFIG_MAX_RAW_LEN
CONFIG_FILE_NAME = "config.bin"  # CONFIG_STORE_FILE_NAME
HEADER_FORMAT = "<IIII"  # magic, format version, JSON length, CRC32
HEADER_LEN = struct.calcsize(HEADER_FORMAT)


class ConfigFormatError(ValueError):
    """A stored config.bin the firmware would refuse to load."""


def pack_config(raw_json: bytes) -> bytes:
    """Return the config.bin contents for raw_json."""
    if not raw_json:
        raise ValueError("config is empty")
    if len(raw_json) > CONFIG_MAX_RAW_LEN:
        raise ValueError(f"config is {len(raw_json)} bytes; the limit is {CONFIG_MAX_RAW_LEN}")
    json.loads(raw_json)  # catch a malformed file here rather than as safe mode on the node
    header = struct.pack(HEADER_FORMAT, CONFIG_MAGIC, CONFIG_FORMAT_VERSION, len(raw_json),
                         zlib.crc32(raw_json))
    return header + raw_json


def unpack_config(packed: bytes) -> bytes:
    """Return the raw JSON stored in config.bin contents.

    Raises ConfigFormatError, naming the first check that failed, for a file
    the firmware would treat as corrupt or too large.
    """
    if len(packed) < HEADER_LEN:
        raise ConfigFormatError(f"stored config is {len(packed)} bytes, shorter than its header")
    magic, version, length, crc = struct.unpack(HEADER_FORMAT, packed[:HEADER_LEN])
    raw_json = packed[HEADER_LEN:]
    if magic != CONFIG_MAGIC:
        raise ConfigFormatError(f"stored config has a bad magic number 0x{magic:08X}")
    if version != CONFIG_FORMAT_VERSION:
        raise ConfigFormatError(f"stored config has unknown format version {version}")
    if length != len(raw_json) or length == 0:
        raise ConfigFormatError(f"stored config header says {length} bytes, "
                                f"but {len(raw_json)} follow it")
    if length > CONFIG_MAX_RAW_LEN:
        raise ConfigFormatError(f"stored config is {length} bytes; the limit is "
                                f"{CONFIG_MAX_RAW_LEN}")
    if zlib.crc32(raw_json) != crc:
        raise ConfigFormatError("stored config fails its CRC check")
    return raw_json
