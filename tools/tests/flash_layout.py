"""The node's flash layout as test data: binary partition tables and flash images.

The layout copies firmware/partitions.csv. Tables are built byte by byte from
the ESP-IDF format (32-byte entries, an MD5 entry, 0xFF padding), not with the
code under test, so a parser bug cannot hide behind a builder with the same bug.
"""

import hashlib
import struct

ENTRY_FORMAT = "<2sBBII16sI"  # magic, type, subtype, offset, size, name, flags
ENTRY_MAGIC = b"\xAA\x50"
MD5_ENTRY_PREFIX = b"\xEB\xEB" + b"\xFF" * 14
TABLE_OFFSET = 0x8000
TABLE_LEN = 0xC00

APP_TYPE = 0x00
DATA_TYPE = 0x01
NVS_SUBTYPE = 0x02
PHY_SUBTYPE = 0x01
FACTORY_SUBTYPE = 0x00
LITTLEFS_SUBTYPE = 0x83

NVS_OFFSET = 0x9000
NVS_SIZE = 0x6000
CONFIG_OFFSET = 0x210000
CONFIG_SIZE = 64 * 1024

# (name, type, subtype, offset, size), as in firmware/partitions.csv
NODE_PARTITIONS = [
    ("nvs", DATA_TYPE, NVS_SUBTYPE, NVS_OFFSET, NVS_SIZE),
    ("phy_init", DATA_TYPE, PHY_SUBTYPE, 0xF000, 0x1000),
    ("factory", APP_TYPE, FACTORY_SUBTYPE, 0x10000, 2 * 1024 * 1024),
    ("config", DATA_TYPE, LITTLEFS_SUBTYPE, CONFIG_OFFSET, CONFIG_SIZE),
]

FLASH_IMAGE_SIZE = CONFIG_OFFSET + CONFIG_SIZE  # just large enough to hold the config partition
NVS_PATTERN = b"mesh keys "  # recognisable stand-in for mesh state in nvs


def partition_table(partitions=NODE_PARTITIONS, *, with_md5=True) -> bytes:
    """Return a binary partition table of TABLE_LEN bytes for (name, type, subtype, offset, size) rows."""
    entries = b"".join(struct.pack(ENTRY_FORMAT, ENTRY_MAGIC, ptype, subtype, offset, size,
                                   name.encode("ascii"), 0)
                       for name, ptype, subtype, offset, size in partitions)
    if with_md5:
        entries += MD5_ENTRY_PREFIX + hashlib.md5(entries).digest()
    return entries.ljust(TABLE_LEN, b"\xFF")


def node_flash_image() -> bytearray:
    """Return an erased flash image with the node's partition table and mesh state in nvs."""
    image = bytearray(b"\xFF" * FLASH_IMAGE_SIZE)
    image[TABLE_OFFSET:TABLE_OFFSET + TABLE_LEN] = partition_table()
    image[NVS_OFFSET:NVS_OFFSET + NVS_SIZE] = (NVS_PATTERN * NVS_SIZE)[:NVS_SIZE]
    return image
