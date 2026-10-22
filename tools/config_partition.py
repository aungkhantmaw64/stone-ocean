"""Find the `config` partition and convert between its image and config.bin.

The partition table is the binary one ESP-IDF writes at 0x8000
(components/partition_table/gen_esp32part.py): 32-byte entries, then an MD5
entry over them, then 0xFF padding. Parsing it here means the tools need
neither IDF_PATH nor a build directory, and always use the layout that is
actually on the node.

The `config` partition holds a LittleFS filesystem with one file, config.bin
(config_format.py). Images are built with the geometry the firmware mounts.
"""

import hashlib
import struct
from typing import NamedTuple

from littlefs import LittleFS, LittleFSError

from config_format import CONFIG_FILE_NAME, pack_config, unpack_config

PARTITION_TABLE_OFFSET = 0x8000  # CONFIG_PARTITION_TABLE_OFFSET
PARTITION_TABLE_LEN = 0xC00  # gen_esp32part.py MAX_PARTITION_LENGTH
CONFIG_PARTITION = "config"

ENTRY_FORMAT = "<2sBBII16sI"  # magic, type, subtype, offset, size, name, flags
ENTRY_LEN = struct.calcsize(ENTRY_FORMAT)
ENTRY_MAGIC = b"\xAA\x50"
MD5_MAGIC = b"\xEB\xEB"
MD5_OFFSET = 16  # the digest is the last 16 bytes of the MD5 entry
END_ENTRY = b"\xFF" * ENTRY_LEN
DATA_PARTITION_TYPE = 0x01

LITTLEFS_BLOCK_SIZE = 4096  # what esp_littlefs uses for a flash partition
LITTLEFS_NAME_MAX = 64  # CONFIG_LITTLEFS_OBJ_NAME_LEN; the firmware keeps the default


class Partition(NamedTuple):
    """One entry of the partition table."""

    name: str
    type: int
    subtype: int
    offset: int
    size: int


class PartitionTableError(ValueError):
    """A partition table that is corrupt or has no usable config partition."""


class ConfigNotFoundError(LookupError):
    """The config partition holds no config.bin."""


def parse_partition_table(table: bytes) -> list:
    """Return the Partition entries of a binary partition table.

    Raises PartitionTableError for an unknown entry, an MD5 mismatch, or a
    table with no end marker.
    """
    entries = []
    for start in range(0, len(table) - ENTRY_LEN + 1, ENTRY_LEN):
        row = table[start:start + ENTRY_LEN]
        if row == END_ENTRY:
            return entries
        if row[:2] == MD5_MAGIC:
            if hashlib.md5(table[:start]).digest() != row[MD5_OFFSET:]:
                raise PartitionTableError("partition table fails its MD5 check")
            continue
        if row[:2] != ENTRY_MAGIC:
            raise PartitionTableError(f"partition table has an unknown entry at byte {start}")
        _, ptype, subtype, offset, size, name, _ = struct.unpack(ENTRY_FORMAT, row)
        entries.append(Partition(name.rstrip(b"\x00").decode("ascii", "replace"), ptype,
                                 subtype, offset, size))
    raise PartitionTableError("partition table has no end marker")


def find_config_partition(table: bytes) -> Partition:
    """Return the `config` partition of a binary partition table."""
    for partition in parse_partition_table(table):
        if partition.name != CONFIG_PARTITION:
            continue
        if partition.type != DATA_PARTITION_TYPE:
            raise PartitionTableError(f"partition '{CONFIG_PARTITION}' is not a data partition")
        if partition.size == 0 or partition.size % LITTLEFS_BLOCK_SIZE != 0:
            raise PartitionTableError(f"partition '{CONFIG_PARTITION}' size {partition.size} "
                                      f"is not a multiple of {LITTLEFS_BLOCK_SIZE}")
        return partition
    raise PartitionTableError(f"partition table has no '{CONFIG_PARTITION}' partition")


def build_config_image(raw_json: bytes, partition_size: int) -> bytes:
    """Return a LittleFS image of partition_size bytes holding raw_json as config.bin."""
    fs = littlefs_for(partition_size)
    fs.format()
    fs.mount()
    with fs.open(CONFIG_FILE_NAME, "wb") as config_file:
        config_file.write(pack_config(raw_json))
    fs.unmount()
    return bytes(fs.context.buffer)


def extract_config(image: bytes) -> bytes:
    """Return the raw JSON stored in a config partition image.

    Raises ConfigNotFoundError when the partition holds no filesystem or no
    config.bin (the firmware then reports "no config stored"), and
    config_format.ConfigFormatError when config.bin is corrupt.
    """
    fs = littlefs_for(len(image))
    fs.context.buffer = bytearray(image)
    try:
        fs.mount()
    except LittleFSError as error:
        raise ConfigNotFoundError("config partition holds no filesystem") from error
    try:
        with fs.open(CONFIG_FILE_NAME, "rb") as config_file:
            packed = config_file.read()
    except FileNotFoundError as error:
        raise ConfigNotFoundError("no config stored") from error
    finally:
        fs.unmount()
    return unpack_config(packed)


def littlefs_for(partition_size: int) -> LittleFS:
    """Return an unmounted LittleFS with the firmware's geometry over an erased buffer."""
    return LittleFS(block_size=LITTLEFS_BLOCK_SIZE,
                    block_count=partition_size // LITTLEFS_BLOCK_SIZE,
                    name_max=LITTLEFS_NAME_MAX, mount=False)
