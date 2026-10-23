/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev_shtc3.h"

#include <stddef.h>
#include <stdint.h>

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief CRC-8 generator polynomial x^8 + x^5 + x^4 + 1 (datasheet). */
#define NODE_DEV_SHTC3_CRC_POLY 0x31U

/** @brief CRC-8 initial value (datasheet). */
#define NODE_DEV_SHTC3_CRC_INIT 0xFFU

/** @brief Most significant bit of a CRC byte. */
#define NODE_DEV_SHTC3_CRC_MSB 0x80U

/** @brief Bits in a byte. */
#define NODE_DEV_SHTC3_BITS_PER_BYTE 8U

/** @brief Full scale of a raw 16-bit word, 2^16, in the conversion
 *         formulas. */
#define NODE_DEV_SHTC3_RAW_FULL_SCALE 65536.0f

/** @brief Temperature offset of the conversion formula, in °C. */
#define NODE_DEV_SHTC3_TEMP_OFFSET_C (-45.0f)

/** @brief Temperature span of the conversion formula, in °C. */
#define NODE_DEV_SHTC3_TEMP_SPAN_C 175.0f

/** @brief Humidity span of the conversion formula, in %. */
#define NODE_DEV_SHTC3_RH_SPAN_PCT 100.0f

/** @brief Lowest humidity reported, in %. */
#define NODE_DEV_SHTC3_RH_MIN_PCT 0.0f

/** @brief Highest humidity reported, in %. */
#define NODE_DEV_SHTC3_RH_MAX_PCT 100.0f

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

uint8_t node_dev_shtc3_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = NODE_DEV_SHTC3_CRC_INIT;
  if (data == NULL) {
    return crc;
  }
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < NODE_DEV_SHTC3_BITS_PER_BYTE; bit++) {
      if ((crc & NODE_DEV_SHTC3_CRC_MSB) != 0U) {
        crc = (uint8_t)((uint8_t)(crc << 1) ^ NODE_DEV_SHTC3_CRC_POLY);
      } else {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return crc;
}

float node_dev_shtc3_raw_to_temp_c(uint16_t raw) {
  return NODE_DEV_SHTC3_TEMP_OFFSET_C +
         (NODE_DEV_SHTC3_TEMP_SPAN_C * (float)raw /
          NODE_DEV_SHTC3_RAW_FULL_SCALE);
}

float node_dev_shtc3_raw_to_rh_pct(uint16_t raw) {
  const float rh_pct =
      NODE_DEV_SHTC3_RH_SPAN_PCT * (float)raw / NODE_DEV_SHTC3_RAW_FULL_SCALE;
  if (rh_pct < NODE_DEV_SHTC3_RH_MIN_PCT) {
    return NODE_DEV_SHTC3_RH_MIN_PCT;
  }
  if (rh_pct > NODE_DEV_SHTC3_RH_MAX_PCT) {
    return NODE_DEV_SHTC3_RH_MAX_PCT;
  }
  return rh_pct;
}
