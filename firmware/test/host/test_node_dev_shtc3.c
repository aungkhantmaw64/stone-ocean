/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_dev_shtc3.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Allowed float rounding error in a converted reading. */
#define CONVERSION_TOLERANCE 0.001f

/** @brief Lowest raw word the sensor can send. */
#define RAW_MIN 0x0000U

/** @brief Middle of the raw range, 2^15. */
#define RAW_MID 0x8000U

/** @brief Highest raw word the sensor can send. */
#define RAW_MAX 0xFFFFU

/** @brief CRC-8 of no data: the initial value (datasheet). */
#define CRC_OF_NO_DATA 0xFFU

/** @brief CRC-8 of 0xBE 0xEF from the datasheet's example. */
#define CRC_OF_DATASHEET_EXAMPLE 0x92U

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Data word of the datasheet's CRC example. */
static const uint8_t g_datasheet_example[] = {0xBE, 0xEF};

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {}

void tearDown(void) {}

/* ---- Checking sensor data ---- */

/* AC: specs/08-shtc3-driver.md #1 */
void test_checksum_of_the_datasheet_example_0xbeef_is_0x92(void) {
  uint8_t crc =
      node_dev_shtc3_crc8(g_datasheet_example, sizeof(g_datasheet_example));

  TEST_ASSERT_EQUAL_HEX8(CRC_OF_DATASHEET_EXAMPLE, crc);
}

/* AC: specs/08-shtc3-driver.md #1 */
void test_checksum_of_no_data_is_the_initial_value_0xff(void) {
  uint8_t crc = node_dev_shtc3_crc8(g_datasheet_example, 0U);

  TEST_ASSERT_EQUAL_HEX8(CRC_OF_NO_DATA, crc);
}

/* ---- Converting raw readings ---- */

/* AC: specs/08-shtc3-driver.md #1 */
void test_lowest_raw_temperature_is_minus_45_degrees_c(void) {
  float temp_c = node_dev_shtc3_raw_to_temp_c(RAW_MIN);

  TEST_ASSERT_FLOAT_WITHIN(CONVERSION_TOLERANCE, -45.0f, temp_c);
}

/* AC: specs/08-shtc3-driver.md #1 */
void test_highest_raw_temperature_is_just_under_130_degrees_c(void) {
  /* -45 + 175 * 65535 / 65536 = 129.99733 °C */
  float temp_c = node_dev_shtc3_raw_to_temp_c(RAW_MAX);

  TEST_ASSERT_FLOAT_WITHIN(CONVERSION_TOLERANCE, 129.99733f, temp_c);
}

/* AC: specs/08-shtc3-driver.md #1 */
void test_mid_scale_raw_temperature_is_42_5_degrees_c(void) {
  /* -45 + 175 / 2 = 42.5 °C */
  float temp_c = node_dev_shtc3_raw_to_temp_c(RAW_MID);

  TEST_ASSERT_FLOAT_WITHIN(CONVERSION_TOLERANCE, 42.5f, temp_c);
}

/* AC: specs/08-shtc3-driver.md #1 */
void test_lowest_raw_humidity_is_0_percent(void) {
  float rh_pct = node_dev_shtc3_raw_to_rh_pct(RAW_MIN);

  TEST_ASSERT_FLOAT_WITHIN(CONVERSION_TOLERANCE, 0.0f, rh_pct);
}

/* AC: specs/08-shtc3-driver.md #1 */
void test_highest_raw_humidity_is_just_under_100_percent(void) {
  /* 100 * 65535 / 65536 = 99.99847 % */
  float rh_pct = node_dev_shtc3_raw_to_rh_pct(RAW_MAX);

  TEST_ASSERT_FLOAT_WITHIN(CONVERSION_TOLERANCE, 99.99847f, rh_pct);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_checksum_of_the_datasheet_example_0xbeef_is_0x92);
  RUN_TEST(test_checksum_of_no_data_is_the_initial_value_0xff);
  RUN_TEST(test_lowest_raw_temperature_is_minus_45_degrees_c);
  RUN_TEST(test_highest_raw_temperature_is_just_under_130_degrees_c);
  RUN_TEST(test_mid_scale_raw_temperature_is_42_5_degrees_c);
  RUN_TEST(test_lowest_raw_humidity_is_0_percent);
  RUN_TEST(test_highest_raw_humidity_is_just_under_100_percent);
  return UNITY_END();
}
