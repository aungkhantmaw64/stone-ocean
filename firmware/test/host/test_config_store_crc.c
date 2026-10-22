/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#include "config_store_crc.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Standard CRC32 check input: the nine ASCII digits "123456789". */
#define CRC32_CHECK_INPUT "123456789"

/** @brief Published CRC32 (IEEE 802.3, as in zlib) of CRC32_CHECK_INPUT. */
#define CRC32_CHECK_VALUE 0xCBF43926u

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {}

void tearDown(void) {}

/* ---- Checksumming the stored config ---- */

/* AC: specs/04-config-storage.md #1 */
void test_checksum_of_the_standard_test_text_matches_the_published_crc32_value(
    void) {
  uint32_t crc =
      config_store_crc_compute(CRC32_CHECK_INPUT, strlen(CRC32_CHECK_INPUT));

  TEST_ASSERT_EQUAL_HEX32(CRC32_CHECK_VALUE, crc);
}

/* AC: specs/04-config-storage.md #1 */
void test_checksum_of_empty_data_is_zero(void) {
  uint32_t crc = config_store_crc_compute("", 0);

  TEST_ASSERT_EQUAL_HEX32(0u, crc);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_checksum_of_the_standard_test_text_matches_the_published_crc32_value);
  RUN_TEST(test_checksum_of_empty_data_is_zero);
  return UNITY_END();
}
