/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_dev_report.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Shortest time between reports in the test policy, in ms. */
#define MIN_INTERVAL_MS 5000u

/** @brief Longest time between reports in the test policy, in ms. */
#define MAX_INTERVAL_MS 60000u

/** @brief Temperature change that triggers a report in the test policy, in
 *         °C. */
#define DELTA_TEMP_C 0.5f

/** @brief Humidity change that triggers a report in the test policy, in
 *         percent. */
#define DELTA_RH_PCT 2.0f

/** @brief Temperature of the first reported reading, in °C. */
#define START_TEMP_C 20.0f

/** @brief Humidity of the first reported reading, in percent. */
#define START_RH_PCT 40.0f

/** @brief HAL time of the first reported reading, in ms. */
#define START_MS 1000u

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Report policy of the tests; reset in setUp(). */
static node_config_report_t g_policy = {0};

/** @brief What was last reported; reset in setUp(). */
static node_dev_report_state_t g_state = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief A reading with this temperature and humidity.
 *
 * @param temp_c Temperature, in °C.
 * @param rh_pct Humidity, in percent.
 * @return The reading.
 */
static node_dev_value_t reading(float temp_c, float rh_pct);

/**
 * @brief Whether a reading is due under g_policy and g_state.
 *
 * @param temp_c Temperature, in °C.
 * @param rh_pct Humidity, in percent.
 * @param now_ms HAL time, in ms.
 * @return The result of node_dev_report_is_due().
 */
static bool reading_is_due(float temp_c, float rh_pct, uint32_t now_ms);

/**
 * @brief Record a reading as reported at @p now_ms.
 *
 * @param temp_c Temperature, in °C.
 * @param rh_pct Humidity, in percent.
 * @param now_ms HAL time, in ms.
 */
static void report_reading(float temp_c, float rh_pct, uint32_t now_ms);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  g_policy = (node_config_report_t){
      .min_interval_ms = MIN_INTERVAL_MS,
      .max_interval_ms = MAX_INTERVAL_MS,
      .delta_temp_c = DELTA_TEMP_C,
      .delta_rh_pct = DELTA_RH_PCT,
  };
  g_state = (node_dev_report_state_t){0};
}

void tearDown(void) {}

/* ---- Reporting sensor readings ---- */

/* AC: specs/06-driver-framework.md #2 */
void test_first_reading_of_a_sensor_is_always_reported(void) {
  TEST_ASSERT_TRUE(reading_is_due(START_TEMP_C, START_RH_PCT, START_MS));
}

/* AC: specs/06-driver-framework.md #2 */
void test_reading_that_changed_less_than_the_delta_is_not_reported(void) {
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);

  const bool due = reading_is_due(START_TEMP_C + 0.4f, START_RH_PCT + 1.9f,
                                  START_MS + MIN_INTERVAL_MS);

  TEST_ASSERT_FALSE(due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_temperature_change_of_at_least_the_delta_is_reported_after_the_minimum_interval(
    void) {
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);

  const bool due = reading_is_due(START_TEMP_C - DELTA_TEMP_C, START_RH_PCT,
                                  START_MS + MIN_INTERVAL_MS);

  TEST_ASSERT_TRUE(due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_humidity_change_of_at_least_the_delta_is_reported_after_the_minimum_interval(
    void) {
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);

  const bool due = reading_is_due(START_TEMP_C, START_RH_PCT + DELTA_RH_PCT,
                                  START_MS + MIN_INTERVAL_MS);

  TEST_ASSERT_TRUE(due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_large_change_is_held_back_until_the_minimum_interval_has_passed(
    void) {
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);
  const float hot_c = START_TEMP_C + 10.0f;

  const bool due_just_before =
      reading_is_due(hot_c, START_RH_PCT, START_MS + MIN_INTERVAL_MS - 1u);
  const bool due_at_minimum =
      reading_is_due(hot_c, START_RH_PCT, START_MS + MIN_INTERVAL_MS);

  TEST_ASSERT_FALSE(due_just_before);
  TEST_ASSERT_TRUE(due_at_minimum);
}

/* AC: specs/06-driver-framework.md #2 */
void test_unchanged_reading_is_reported_when_the_maximum_interval_has_passed(
    void) {
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);

  const bool due_just_before = reading_is_due(START_TEMP_C, START_RH_PCT,
                                              START_MS + MAX_INTERVAL_MS - 1u);
  const bool due_at_maximum =
      reading_is_due(START_TEMP_C, START_RH_PCT, START_MS + MAX_INTERVAL_MS);

  TEST_ASSERT_FALSE(due_just_before);
  TEST_ASSERT_TRUE(due_at_maximum);
}

/* AC: specs/06-driver-framework.md #2 */
void test_delta_is_measured_against_the_last_reported_value_not_the_last_sample(
    void) {
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);
  const uint32_t later_ms = START_MS + MIN_INTERVAL_MS;

  /* Two samples creep up by 0.3 °C each; neither alone reaches the 0.5 °C
   * delta, but together they are 0.6 °C above the last reported value. */
  const bool first_step_due =
      reading_is_due(START_TEMP_C + 0.3f, START_RH_PCT, later_ms);
  const bool second_step_due =
      reading_is_due(START_TEMP_C + 0.6f, START_RH_PCT, later_ms + 1000u);

  TEST_ASSERT_FALSE(first_step_due);
  TEST_ASSERT_TRUE(second_step_due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_any_change_is_reported_when_the_delta_is_zero(void) {
  g_policy.delta_temp_c = 0.0f;
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);

  const bool due = reading_is_due(START_TEMP_C + 0.01f, START_RH_PCT,
                                  START_MS + MIN_INTERVAL_MS);

  TEST_ASSERT_TRUE(due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_unchanged_reading_is_not_reported_when_the_delta_is_zero(void) {
  g_policy.delta_temp_c = 0.0f;
  g_policy.delta_rh_pct = 0.0f;
  report_reading(START_TEMP_C, START_RH_PCT, START_MS);

  const bool due =
      reading_is_due(START_TEMP_C, START_RH_PCT, START_MS + MIN_INTERVAL_MS);

  TEST_ASSERT_FALSE(due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_reporting_intervals_are_measured_correctly_across_the_millisecond_clock_wrap_around(
    void) {
  /* Reported 1 s before the 32-bit millisecond clock wraps to 0. */
  const uint32_t before_wrap_ms = UINT32_MAX - 999u;
  report_reading(START_TEMP_C, START_RH_PCT, before_wrap_ms);
  const float hot_c = START_TEMP_C + 1.0f;

  const bool due_before_minimum =
      reading_is_due(hot_c, START_RH_PCT, before_wrap_ms + 2000u);
  const bool due_at_minimum =
      reading_is_due(hot_c, START_RH_PCT, before_wrap_ms + MIN_INTERVAL_MS);
  const bool heartbeat_due = reading_is_due(START_TEMP_C, START_RH_PCT,
                                            before_wrap_ms + MAX_INTERVAL_MS);

  TEST_ASSERT_FALSE(due_before_minimum);
  TEST_ASSERT_TRUE(due_at_minimum);
  TEST_ASSERT_TRUE(heartbeat_due);
}

/* ---- Reporting motion and LED state ---- */

/* AC: specs/06-driver-framework.md #2 */
void test_first_motion_and_led_states_are_always_reported(void) {
  const node_dev_value_t idle = {0};

  const bool motion_due = node_dev_report_is_due(&g_state, NODE_DEV_SIG_MOTION,
                                                 NULL, &idle, START_MS);
  const bool led_due = node_dev_report_is_due(
      &g_state, NODE_DEV_SIG_ONOFF_CHANGED, NULL, &idle, START_MS);

  TEST_ASSERT_TRUE(motion_due);
  TEST_ASSERT_TRUE(led_due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_motion_is_reported_only_when_it_starts_or_stops(void) {
  const node_dev_value_t still = {.motion = false};
  const node_dev_value_t moving = {.motion = true};
  node_dev_report_record(&g_state, &still, START_MS);

  const bool still_due = node_dev_report_is_due(&g_state, NODE_DEV_SIG_MOTION,
                                                NULL, &still, START_MS + 1u);
  const bool start_due = node_dev_report_is_due(&g_state, NODE_DEV_SIG_MOTION,
                                                NULL, &moving, START_MS + 1u);

  TEST_ASSERT_FALSE(still_due);
  TEST_ASSERT_TRUE(start_due);
}

/* AC: specs/06-driver-framework.md #2 */
void test_led_state_is_reported_only_when_it_changes(void) {
  const node_dev_value_t off = {.on = false};
  const node_dev_value_t on = {.on = true};
  node_dev_report_record(&g_state, &on, START_MS);

  const bool same_due = node_dev_report_is_due(
      &g_state, NODE_DEV_SIG_ONOFF_CHANGED, NULL, &on, START_MS + 1u);
  const bool change_due = node_dev_report_is_due(
      &g_state, NODE_DEV_SIG_ONOFF_CHANGED, NULL, &off, START_MS + 1u);

  TEST_ASSERT_FALSE(same_due);
  TEST_ASSERT_TRUE(change_due);
}

/* ---- Rejecting bad input ---- */

/* AC: specs/06-driver-framework.md #2 */
void test_report_check_with_missing_arguments_reports_nothing(void) {
  const node_dev_value_t sample = reading(START_TEMP_C, START_RH_PCT);

  TEST_ASSERT_FALSE(node_dev_report_is_due(NULL, NODE_DEV_SIG_READING,
                                           &g_policy, &sample, START_MS));
  TEST_ASSERT_FALSE(node_dev_report_is_due(&g_state, NODE_DEV_SIG_READING,
                                           &g_policy, NULL, START_MS));
  TEST_ASSERT_FALSE(node_dev_report_is_due(&g_state, NODE_DEV_SIG_READING, NULL,
                                           &sample, START_MS));
  TEST_ASSERT_FALSE(node_dev_report_is_due(&g_state, NODE_DEV_SIG_FAULT,
                                           &g_policy, &sample, START_MS));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_first_reading_of_a_sensor_is_always_reported);
  RUN_TEST(test_reading_that_changed_less_than_the_delta_is_not_reported);
  RUN_TEST(
      test_temperature_change_of_at_least_the_delta_is_reported_after_the_minimum_interval);
  RUN_TEST(
      test_humidity_change_of_at_least_the_delta_is_reported_after_the_minimum_interval);
  RUN_TEST(
      test_large_change_is_held_back_until_the_minimum_interval_has_passed);
  RUN_TEST(
      test_unchanged_reading_is_reported_when_the_maximum_interval_has_passed);
  RUN_TEST(
      test_delta_is_measured_against_the_last_reported_value_not_the_last_sample);
  RUN_TEST(test_any_change_is_reported_when_the_delta_is_zero);
  RUN_TEST(test_unchanged_reading_is_not_reported_when_the_delta_is_zero);
  RUN_TEST(
      test_reporting_intervals_are_measured_correctly_across_the_millisecond_clock_wrap_around);
  RUN_TEST(test_first_motion_and_led_states_are_always_reported);
  RUN_TEST(test_motion_is_reported_only_when_it_starts_or_stops);
  RUN_TEST(test_led_state_is_reported_only_when_it_changes);
  RUN_TEST(test_report_check_with_missing_arguments_reports_nothing);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_dev_value_t reading(float temp_c, float rh_pct) {
  const node_dev_value_t value = {.temp_c = temp_c, .rh_pct = rh_pct};
  return value;
}

static bool reading_is_due(float temp_c, float rh_pct, uint32_t now_ms) {
  const node_dev_value_t sample = reading(temp_c, rh_pct);
  return node_dev_report_is_due(&g_state, NODE_DEV_SIG_READING, &g_policy,
                                &sample, now_ms);
}

static void report_reading(float temp_c, float rh_pct, uint32_t now_ms) {
  const node_dev_value_t value = reading(temp_c, rh_pct);
  node_dev_report_record(&g_state, &value, now_ms);
}
