/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "config_store_crc.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Reflected form of the IEEE 802.3 CRC32 polynomial 0x04C11DB7. */
#define CONFIG_STORE_CRC_POLY 0xEDB88320U

/** @brief Initial register value, and the value XORed into the result. */
#define CONFIG_STORE_CRC_INIT_XOR 0xFFFFFFFFU

/** @brief Number of bits in a byte, processed one at a time. */
#define CONFIG_STORE_CRC_BITS_PER_BYTE 8U

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

uint32_t config_store_crc_compute(const void *data, size_t len) {
  if (data == NULL || len == 0) {
    return 0;
  }
  /* Bitwise rather than table-driven: a 4 KB config is checksummed once per
   * boot or upload, so 1 KB of table is not worth its flash. */
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = CONFIG_STORE_CRC_INIT_XOR;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint32_t)bytes[i];
    for (uint32_t bit = 0; bit < CONFIG_STORE_CRC_BITS_PER_BYTE; bit++) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1) ^ CONFIG_STORE_CRC_POLY;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ CONFIG_STORE_CRC_INIT_XOR;
}
