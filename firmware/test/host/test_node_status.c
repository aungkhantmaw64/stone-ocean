/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_ao_port_mock.h"
#include "node_hal.h"
#include "node_hal_board.h"
#include "node_hal_mock.h"
#include "node_status.h"
#include "node_status_internal.h"
#include "node_status_log_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Pin of the status LED. */
#define LED_GPIO NODE_HAL_BOARD_STATUS_LED_GPIO

/** @brief Pin level that lights the active-low status LED. */
#define LIT_LEVEL 0

/** @brief Pin level that darkens the active-low status LED. */
#define DARK_LEVEL 1

/** @brief Commands the status AO's queue holds (spec 16). */
#define QUEUE_LEN 4

/** @brief A blink on time that is valid and easy to step through, in ms. */
#define ON_MS 100

/** @brief A blink off time that differs from ON_MS, in ms. */
#define OFF_MS 300

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/** @brief Start the status AO and run until its init event is handled. */
static void start_status_led(void);

/** @brief Run every AO until idle; fail the test on a runaway. */
static void run_until_idle(void);

/**
 * @brief Let @p ms of virtual time pass, then run every AO until idle.
 *
 * @param ms Time to let pass, in ms.
 */
static void wait_ms(uint32_t ms);

/**
 * @brief Level of the status LED's pin.
 *
 * @return LIT_LEVEL or DARK_LEVEL.
 */
static int led_level(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_status_reset();
  node_status_log_mock_reset();
}

void tearDown(void) {}

/* ---- Starting the status LED ---- */

/* AC: specs/16-status-led.md #1 */
void test_status_led_is_switched_off_when_it_starts(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(DARK_LEVEL, led_level());
  TEST_ASSERT_EQUAL_size_t(1, node_hal_gpio_mock_write_count(LED_GPIO));
}

/* AC: specs/16-status-led.md #2 */
void test_status_led_cannot_be_started_twice(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_ERR_STATE, node_status_start());
}

/* AC: specs/16-status-led.md #1 */
void test_status_led_ignores_every_command_when_its_pin_cannot_be_set_up(void) {
  node_hal_gpio_mock_fail_next_config(NODE_HAL_ERR_IO);
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  wait_ms(ON_MS + OFF_MS);

  TEST_ASSERT_EQUAL_size_t(0, node_hal_gpio_mock_write_count(LED_GPIO));
  TEST_ASSERT_EQUAL_size_t(0, node_status_log_mock_steady_count());
  TEST_ASSERT_EQUAL_size_t(0, node_status_log_mock_blink_count());
}

/* AC: specs/16-status-led.md #1 */
void test_failed_setup_of_the_status_led_pin_is_logged_as_a_warning(void) {
  node_hal_gpio_mock_fail_next_config(NODE_HAL_ERR_IO);

  start_status_led();

  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_setup_failed_count());
  TEST_ASSERT_EQUAL_size_t(0, node_status_log_mock_started_count());
}

/* ---- Switching on and off ---- */

/* AC: specs/16-status-led.md #1 */
void test_status_led_lights_by_driving_its_pin_low_when_switched_on(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(LIT_LEVEL, led_level());
}

/* AC: specs/16-status-led.md #1 */
void test_status_led_goes_dark_by_driving_its_pin_high_when_switched_off(void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(false));
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(DARK_LEVEL, led_level());
}

/* ---- Checking commands ---- */

/* AC: specs/16-status-led.md #2 */
void test_blink_with_an_on_time_below_the_minimum_is_rejected(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_ERR_ARG,
                        node_status_led_blink(NODE_STATUS_BLINK_MIN_MS - 1,
                                              NODE_STATUS_BLINK_MIN_MS));
}

/* AC: specs/16-status-led.md #2 */
void test_blink_with_an_off_time_below_the_minimum_is_rejected(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_ERR_ARG,
                        node_status_led_blink(NODE_STATUS_BLINK_MIN_MS,
                                              NODE_STATUS_BLINK_MIN_MS - 1));
}

/* AC: specs/16-status-led.md #2 */
void test_blink_at_exactly_the_minimum_on_and_off_times_is_accepted(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK,
                        node_status_led_blink(NODE_STATUS_BLINK_MIN_MS,
                                              NODE_STATUS_BLINK_MIN_MS));
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(LIT_LEVEL, led_level());
  wait_ms(NODE_STATUS_BLINK_MIN_MS);
  TEST_ASSERT_EQUAL_INT(DARK_LEVEL, led_level());
}

/* AC: specs/16-status-led.md #2 */
void test_status_led_commands_are_rejected_before_it_starts(void) {
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_ERR_STATE, node_status_led_set(true));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_ERR_STATE,
                        node_status_led_blink(ON_MS, OFF_MS));
}

/* AC: specs/16-status-led.md #2 */
void test_status_led_command_is_rejected_without_waiting_when_its_queue_is_full(
    void) {
  start_status_led();
  for (int i = 0; i < QUEUE_LEN; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set((i % 2) == 0));
  }

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_ERR_FULL, node_status_led_set(true));
}

/* AC: specs/16-status-led.md #1 */
void test_latest_status_led_command_wins_when_several_are_queued(void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(false));

  run_until_idle();
  const size_t writes = node_hal_gpio_mock_write_count(LED_GPIO);
  wait_ms(ON_MS + OFF_MS);

  TEST_ASSERT_EQUAL_INT(DARK_LEVEL, led_level());
  TEST_ASSERT_EQUAL_size_t(writes, node_hal_gpio_mock_write_count(LED_GPIO));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_status_led_is_switched_off_when_it_starts);
  RUN_TEST(test_status_led_cannot_be_started_twice);
  RUN_TEST(test_status_led_ignores_every_command_when_its_pin_cannot_be_set_up);
  RUN_TEST(test_failed_setup_of_the_status_led_pin_is_logged_as_a_warning);
  RUN_TEST(test_status_led_lights_by_driving_its_pin_low_when_switched_on);
  RUN_TEST(test_status_led_goes_dark_by_driving_its_pin_high_when_switched_off);
  RUN_TEST(test_blink_with_an_on_time_below_the_minimum_is_rejected);
  RUN_TEST(test_blink_with_an_off_time_below_the_minimum_is_rejected);
  RUN_TEST(test_blink_at_exactly_the_minimum_on_and_off_times_is_accepted);
  RUN_TEST(test_status_led_commands_are_rejected_before_it_starts);
  RUN_TEST(
      test_status_led_command_is_rejected_without_waiting_when_its_queue_is_full);
  RUN_TEST(test_latest_status_led_command_wins_when_several_are_queued);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void start_status_led(void) {
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_start());
  run_until_idle();
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static void wait_ms(uint32_t ms) {
  node_hal_time_mock_advance(ms);
  run_until_idle();
}

static int led_level(void) { return node_hal_gpio_read(LED_GPIO); }
