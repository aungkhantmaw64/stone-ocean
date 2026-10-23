/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_ao_port_mock.h"
#include "node_hal.h"
#include "node_hal_board.h"
#include "node_hal_mock.h"
#include "node_health.h"
#include "node_health_internal.h"
#include "node_health_log_mock.h"
#include "node_health_port_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Pin of the reset button. */
#define BUTTON_GPIO NODE_HAL_BOARD_RESET_BUTTON_GPIO

/** @brief Pin level of a pressed button; it is active low. */
#define PRESSED_LEVEL 0

/** @brief Pin level of a released button, held by the board's pull-up. */
#define RELEASED_LEVEL 1

/** @brief Hold time that triggers a factory reset, in ms (spec 09). */
#define HOLD_MS NODE_HEALTH_RESET_HOLD_MS

/** @brief A press just short of the hold time, in ms (spec 09). */
#define SHORT_PRESS_MS 4900

/** @brief Time watched after a press ends, in ms; longer than the hold time,
 *         so a late factory reset would be seen. */
#define WATCH_MS 10000

/** @brief Time between two contact bounces, in ms. */
#define BOUNCE_MS 5

/** @brief Mock clock at start in the wrap-around test: the hold time ends
 *         after the 32-bit millisecond clock wraps to 0. */
#define NEAR_CLOCK_WRAP_MS (UINT32_MAX - 1000u)

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/** @brief Start the health AO and run its init event, with the button
 *         held down. */
static void start_with_button_pressed(void);

/** @brief Start the health AO and run its init event, with the button
 *         released by the board's pull-up. */
static void start_with_button_released(void);

/** @brief Press the button and let the health AO handle the edge. */
static void press(void);

/** @brief Release the button and let the health AO handle the edge. */
static void release(void);

/**
 * @brief Let @p ms of virtual time pass, then run every AO until idle.
 *
 * @param ms Time to let pass, in ms.
 */
static void wait_ms(uint32_t ms);

/** @brief Run every AO until idle; fail the test on a runaway. */
static void run_until_idle(void);

/**
 * @brief Number of factory resets so far; each one restarts the node.
 *
 * @return The count.
 */
static size_t factory_resets(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_health_ao_reset();
  node_health_reset();
  node_health_port_mock_reset();
  node_health_log_mock_reset();
}

void tearDown(void) {}

/* ---- Long press on the reset button ---- */

/* AC: specs/09-health-factory-reset.md #3, #5 */
void test_holding_the_reset_button_for_5_seconds_factory_resets_the_node(void) {
  start_with_button_released();
  press();

  wait_ms(HOLD_MS - 1);
  TEST_ASSERT_EQUAL_size_t(0, factory_resets());

  wait_ms(1);
  TEST_ASSERT_EQUAL_size_t(1, factory_resets());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_factory_reset_count());
}

/* AC: specs/09-health-factory-reset.md #3, #5 */
void test_releasing_the_reset_button_after_4_9_seconds_does_not_factory_reset_the_node(
    void) {
  start_with_button_released();
  press();
  wait_ms(SHORT_PRESS_MS);

  release();
  wait_ms(WATCH_MS);

  TEST_ASSERT_EQUAL_size_t(0, factory_resets());
  TEST_ASSERT_EQUAL_size_t(1,
                           node_health_log_mock_button_released_early_count());
}

/* AC: specs/09-health-factory-reset.md #5 */
void test_a_bouncing_press_is_treated_as_one_press_and_factory_resets_5_seconds_after_it_settles(
    void) {
  start_with_button_released();
  press();
  wait_ms(BOUNCE_MS);
  release();
  wait_ms(BOUNCE_MS);
  press();

  wait_ms(HOLD_MS - 1);
  TEST_ASSERT_EQUAL_size_t(0, factory_resets());

  wait_ms(1);
  TEST_ASSERT_EQUAL_size_t(1, factory_resets());
}

/* AC: specs/09-health-factory-reset.md #3, #5 */
void test_a_press_already_held_at_boot_does_not_factory_reset_the_node(void) {
  start_with_button_pressed();

  wait_ms(WATCH_MS);

  TEST_ASSERT_EQUAL_size_t(0, factory_resets());
  TEST_ASSERT_EQUAL_size_t(0, node_health_log_mock_button_held_count());
}

/* AC: specs/09-health-factory-reset.md #5 */
void test_a_new_press_after_releasing_the_boot_press_factory_resets_the_node(
    void) {
  start_with_button_pressed();
  wait_ms(WATCH_MS);
  release();

  press();
  wait_ms(HOLD_MS);

  TEST_ASSERT_EQUAL_size_t(1, factory_resets());
}

/* AC: specs/09-health-factory-reset.md #5 */
void test_no_factory_reset_happens_if_the_button_was_released_while_its_release_went_unnoticed(
    void) {
  start_with_button_released();
  press();
  wait_ms(SHORT_PRESS_MS);

  node_hal_gpio_mock_set_input_without_edge(BUTTON_GPIO, RELEASED_LEVEL);
  wait_ms(WATCH_MS);

  TEST_ASSERT_EQUAL_size_t(0, factory_resets());
}

/* AC: specs/09-health-factory-reset.md #5 */
void test_a_long_press_works_across_the_wrap_around_of_the_millisecond_clock(
    void) {
  node_hal_time_mock_set(NEAR_CLOCK_WRAP_MS);
  start_with_button_released();
  press();

  wait_ms(HOLD_MS - 1);
  TEST_ASSERT_EQUAL_size_t(0, factory_resets());

  wait_ms(1);
  TEST_ASSERT_EQUAL_size_t(1, factory_resets());
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_the_reset_button_does_nothing_when_its_pin_cannot_be_set_up(void) {
  node_hal_gpio_mock_fail_next_config(NODE_HAL_ERR_IO);
  start_with_button_released();

  press();
  wait_ms(WATCH_MS);

  TEST_ASSERT_EQUAL_size_t(0, factory_resets());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_button_setup_failed_count());
  TEST_ASSERT_EQUAL_size_t(0, node_health_log_mock_button_started_count());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_holding_the_reset_button_for_5_seconds_factory_resets_the_node);
  RUN_TEST(
      test_releasing_the_reset_button_after_4_9_seconds_does_not_factory_reset_the_node);
  RUN_TEST(
      test_a_bouncing_press_is_treated_as_one_press_and_factory_resets_5_seconds_after_it_settles);
  RUN_TEST(test_a_press_already_held_at_boot_does_not_factory_reset_the_node);
  RUN_TEST(
      test_a_new_press_after_releasing_the_boot_press_factory_resets_the_node);
  RUN_TEST(
      test_no_factory_reset_happens_if_the_button_was_released_while_its_release_went_unnoticed);
  RUN_TEST(
      test_a_long_press_works_across_the_wrap_around_of_the_millisecond_clock);
  RUN_TEST(test_the_reset_button_does_nothing_when_its_pin_cannot_be_set_up);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void start_with_button_pressed(void) {
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK, node_health_ao_start());
  run_until_idle();
  /* The mock reads an input with no internal pull as low after setup, which
   * is the pressed level. */
  TEST_ASSERT_EQUAL_INT(PRESSED_LEVEL, node_hal_gpio_read(BUTTON_GPIO));
}

static void start_with_button_released(void) {
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK, node_health_ao_start());
  run_until_idle();
  /* The mock reads the pin low after setup, where the board's pull-up would
   * hold it high; a release edge brings it to the released state. */
  release();
  TEST_ASSERT_EQUAL_size_t(0,
                           node_health_log_mock_button_released_early_count());
}

static void press(void) {
  node_hal_gpio_mock_set_input(BUTTON_GPIO, PRESSED_LEVEL);
  run_until_idle();
}

static void release(void) {
  node_hal_gpio_mock_set_input(BUTTON_GPIO, RELEASED_LEVEL);
  run_until_idle();
}

static void wait_ms(uint32_t ms) {
  node_hal_time_mock_advance(ms);
  run_until_idle();
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static size_t factory_resets(void) {
  return node_health_port_mock_restart_count();
}
