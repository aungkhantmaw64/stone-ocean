#include <stdint.h>

#include "node_hal_mock.h"
#include "unity.h"

#define ONE_SECOND_MS 1000
#define SENSOR_INTERVAL_MS 250
#define MS_BEFORE_WRAP 100

void setUp(void) { node_hal_mock_reset(); }

void tearDown(void) {}

/* ---- Virtual clock ---- */

/* AC: specs/02-hal.md #3 */
void test_clock_starts_at_zero_after_reset(void) {
  node_hal_time_mock_advance(ONE_SECOND_MS);

  node_hal_mock_reset();

  TEST_ASSERT_EQUAL_UINT32(0, node_hal_time_ms());
}

/* AC: specs/02-hal.md #3 */
void test_clock_moves_forward_by_the_advanced_amount(void) {
  node_hal_time_mock_advance(ONE_SECOND_MS);
  node_hal_time_mock_advance(SENSOR_INTERVAL_MS);

  TEST_ASSERT_EQUAL_UINT32(ONE_SECOND_MS + SENSOR_INTERVAL_MS,
                           node_hal_time_ms());
}

/* AC: specs/02-hal.md #3 */
void test_delay_moves_the_clock_forward_by_the_delay(void) {
  node_hal_time_mock_set(ONE_SECOND_MS);

  node_hal_time_delay_ms(SENSOR_INTERVAL_MS);

  TEST_ASSERT_EQUAL_UINT32(ONE_SECOND_MS + SENSOR_INTERVAL_MS,
                           node_hal_time_ms());
}

/* AC: specs/02-hal.md #3 */
void test_clock_wraps_to_zero_after_about_49_days_like_the_real_clock(void) {
  /* A uint32_t millisecond counter overflows after 2^32 ms, about 49.7 days. */
  node_hal_time_mock_set(UINT32_MAX - MS_BEFORE_WRAP + 1);

  node_hal_time_mock_advance(MS_BEFORE_WRAP + SENSOR_INTERVAL_MS);

  TEST_ASSERT_EQUAL_UINT32(SENSOR_INTERVAL_MS, node_hal_time_ms());
}

/* AC: specs/02-hal.md #3 */
void test_elapsed_time_is_correct_across_the_clock_wrap(void) {
  node_hal_time_mock_set(UINT32_MAX - MS_BEFORE_WRAP + 1);
  uint32_t start_ms = node_hal_time_ms();

  node_hal_time_mock_advance(ONE_SECOND_MS);

  TEST_ASSERT_EQUAL_UINT32(ONE_SECOND_MS, node_hal_time_ms() - start_ms);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_clock_starts_at_zero_after_reset);
  RUN_TEST(test_clock_moves_forward_by_the_advanced_amount);
  RUN_TEST(test_delay_moves_the_clock_forward_by_the_delay);
  RUN_TEST(test_clock_wraps_to_zero_after_about_49_days_like_the_real_clock);
  RUN_TEST(test_elapsed_time_is_correct_across_the_clock_wrap);
  return UNITY_END();
}
