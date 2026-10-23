/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>

#include "node_hal_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief One second, in ms. */
#define ONE_SECOND_MS 1000

/** @brief Example sensor polling interval, in ms. */
#define SENSOR_INTERVAL_MS 250

/** @brief A sensor's wake-up time, in µs; four of them add up to just under
 *         one ms. */
#define WAKE_UP_TIME_US 240U

/** @brief Number of wake-up waits that still add up to under one ms
 *         (4 x 240 µs = 960 µs). */
#define WAITS_UNDER_ONE_MS 4

/** @brief How long before the clock wraps the wrap-around tests start, in ms.
 */
#define MS_BEFORE_WRAP 100

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

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

/* ---- Short busy waits ---- */

/* AC: specs/02-hal.md #3 */
void test_busy_wait_moves_the_clock_forward_once_the_waits_add_up_to_a_millisecond(
    void) {
  for (int i = 0; i < WAITS_UNDER_ONE_MS; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_HAL_OK,
                          node_hal_time_busy_wait_us(WAKE_UP_TIME_US));
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, node_hal_time_ms(),
                                   "960 µs is still under one ms");

  TEST_ASSERT_EQUAL_INT(NODE_HAL_OK,
                        node_hal_time_busy_wait_us(WAKE_UP_TIME_US));

  TEST_ASSERT_EQUAL_UINT32(1, node_hal_time_ms());
  TEST_ASSERT_EQUAL_UINT64((WAITS_UNDER_ONE_MS + 1U) * WAKE_UP_TIME_US,
                           node_hal_time_mock_busy_wait_total_us());
}

/* AC: specs/02-hal.md #3 */
void test_busy_wait_longer_than_one_millisecond_is_rejected_and_waits_nothing(
    void) {
  node_hal_err_t err =
      node_hal_time_busy_wait_us(NODE_HAL_TIME_BUSY_WAIT_MAX_US + 1U);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG, err);
  TEST_ASSERT_EQUAL_UINT32(0, node_hal_time_ms());
  TEST_ASSERT_EQUAL_UINT64(0, node_hal_time_mock_busy_wait_total_us());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_clock_starts_at_zero_after_reset);
  RUN_TEST(test_clock_moves_forward_by_the_advanced_amount);
  RUN_TEST(test_delay_moves_the_clock_forward_by_the_delay);
  RUN_TEST(test_clock_wraps_to_zero_after_about_49_days_like_the_real_clock);
  RUN_TEST(test_elapsed_time_is_correct_across_the_clock_wrap);
  RUN_TEST(
      test_busy_wait_moves_the_clock_forward_once_the_waits_add_up_to_a_millisecond);
  RUN_TEST(
      test_busy_wait_longer_than_one_millisecond_is_rejected_and_waits_nothing);
  return UNITY_END();
}
