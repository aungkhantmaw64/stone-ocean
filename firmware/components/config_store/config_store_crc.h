#pragma once

/**
 * @file config_store_crc.h
 * @brief CRC32 used to guard the stored config. Not part of the public
 *        interface.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief CRC32 (IEEE 802.3) of a byte buffer.
 *
 * Reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, final XOR
 * 0xFFFFFFFF: the CRC of the ASCII bytes "123456789" is 0xCBF43926, the same
 * as zlib's crc32().
 *
 * @param data Bytes to checksum; may be NULL only if @p len is 0.
 * @param len  Number of bytes in @p data.
 * @return The CRC32, or 0 for empty or NULL input.
 */
uint32_t config_store_crc_compute(const void *data, size_t len);
