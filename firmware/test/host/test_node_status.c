/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_ao.h"
#include "node_ao_port_mock.h"
#include "node_ao_pub_sigs.h"
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

/** @brief Blink periods a pattern is followed for. */
#define PERIODS 5

/** @brief Time a steady LED is watched for after blinking ends, in ms; several
 *         blink periods. */
#define WATCH_MS 2000

/** @brief Events the recording subscriber's queue holds. */
#define SUBSCRIBER_QUEUE_LEN 4

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief A test AO that counts the published events it receives. */
typedef struct {
  node_ao_t super; /**< Framework part; first. */
  size_t count;    /**< Events received, init event excluded. */
} subscriber_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Subscriber to every published signal; reset in setUp(). */
static subscriber_t g_subscriber = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/** @brief Start the status AO and run until its init event is handled. */
static void start_status_led(void);

/** @brief Run every AO until idle; fail the test on a runaway. */
static void run_until_idle(void);

/**
 * @brief Check the LED follows a blink pattern for PERIODS periods, starting
 *        lit at the beginning of an on phase, and switches exactly at the end
 *        of each phase.
 *
 * @param on_ms  Expected on time, in ms; at least 2.
 * @param off_ms Expected off time, in ms; at least 2.
 */
static void assert_blinks(uint32_t on_ms, uint32_t off_ms);

/**
 * @brief Count an event in the subscriber.
 *
 * @param me The subscriber.
 * @param e  The event.
 */
static void subscriber_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/** @brief Start g_subscriber and subscribe it to every published signal. */
static void start_subscriber(void);

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
  g_subscriber = (subscriber_t){0};
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

/* ---- Blinking ---- */

/* AC: specs/16-status-led.md #1 */
void test_status_led_blinks_with_the_requested_unequal_on_and_off_times_over_several_periods(
    void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();

  assert_blinks(ON_MS, OFF_MS);
}

/* AC: specs/16-status-led.md #1 */
void test_status_led_keeps_blinking_on_time_across_the_millisecond_clock_wrap_around(
    void) {
  /* The 32-bit clock wraps during the first period. */
  node_hal_time_mock_set(UINT32_MAX - (ON_MS / 2));
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();

  assert_blinks(ON_MS, OFF_MS);
}

/* AC: specs/16-status-led.md #1 */
void test_new_blink_command_restarts_the_pattern_with_the_on_phase(void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();
  wait_ms(ON_MS + (OFF_MS / 2));
  TEST_ASSERT_EQUAL_INT(DARK_LEVEL, led_level());

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();

  assert_blinks(ON_MS, OFF_MS);
}

/* AC: specs/16-status-led.md #1 */
void test_switching_the_status_led_on_ends_blinking(void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();
  wait_ms(ON_MS + (OFF_MS / 2));

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  run_until_idle();
  const size_t writes = node_hal_gpio_mock_write_count(LED_GPIO);
  wait_ms(WATCH_MS);

  TEST_ASSERT_EQUAL_INT(LIT_LEVEL, led_level());
  TEST_ASSERT_EQUAL_size_t(writes, node_hal_gpio_mock_write_count(LED_GPIO));
}

/* AC: specs/16-status-led.md #1 */
void test_switching_the_status_led_off_ends_blinking(void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();
  wait_ms(ON_MS / 2);

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(false));
  run_until_idle();
  const size_t writes = node_hal_gpio_mock_write_count(LED_GPIO);
  wait_ms(WATCH_MS);

  TEST_ASSERT_EQUAL_INT(DARK_LEVEL, led_level());
  TEST_ASSERT_EQUAL_size_t(writes, node_hal_gpio_mock_write_count(LED_GPIO));
}

/* ---- Logging ---- */

/* AC: specs/16-status-led.md #1 */
void test_change_of_status_led_mode_is_logged(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  run_until_idle();
  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_steady_count());
  TEST_ASSERT_TRUE(node_status_log_mock_last_steady_on());

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();
  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_blink_count());
  TEST_ASSERT_EQUAL_UINT16(ON_MS, node_status_log_mock_last_blink_on_ms());
  TEST_ASSERT_EQUAL_UINT16(OFF_MS, node_status_log_mock_last_blink_off_ms());

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(false));
  run_until_idle();
  TEST_ASSERT_EQUAL_size_t(2, node_status_log_mock_steady_count());
  TEST_ASSERT_FALSE(node_status_log_mock_last_steady_on());
}

/* AC: specs/16-status-led.md #1 */
void test_repeating_the_current_status_led_command_is_not_logged(void) {
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_steady_count());
  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_blink_count());
}

/* AC: specs/16-status-led.md #1 */
void test_individual_blink_toggles_are_not_logged(void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();

  wait_ms(PERIODS * (ON_MS + OFF_MS));

  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_blink_count());
  TEST_ASSERT_EQUAL_size_t(0, node_status_log_mock_steady_count());
}

/* AC: specs/16-status-led.md #1 */
void test_failing_status_led_pin_writes_are_logged_once_per_failure_streak(
    void) {
  start_status_led();
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, ON_MS));
  run_until_idle();

  /* First streak: two phases in a row fail, then one works. */
  node_hal_gpio_mock_fail_next_write(NODE_HAL_ERR_IO);
  wait_ms(ON_MS);
  node_hal_gpio_mock_fail_next_write(NODE_HAL_ERR_IO);
  wait_ms(ON_MS);
  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_write_failed_count());
  wait_ms(ON_MS);

  /* Second streak. */
  node_hal_gpio_mock_fail_next_write(NODE_HAL_ERR_IO);
  wait_ms(ON_MS);

  TEST_ASSERT_EQUAL_size_t(2, node_status_log_mock_write_failed_count());
}

/* AC: specs/16-status-led.md #1 */
void test_status_led_pin_working_again_after_failed_writes_is_logged(void) {
  start_status_led();
  node_hal_gpio_mock_fail_next_write(NODE_HAL_ERR_IO);
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  run_until_idle();
  TEST_ASSERT_EQUAL_size_t(0, node_status_log_mock_write_recovered_count());

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, node_status_log_mock_write_recovered_count());
  TEST_ASSERT_EQUAL_INT(LIT_LEVEL, led_level());
}

/* ---- Isolation ---- */

/* AC: specs/16-status-led.md #5 */
void test_status_led_publishes_no_events_to_subscribers(void) {
  start_subscriber();
  start_status_led();

  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(true));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_blink(ON_MS, OFF_MS));
  run_until_idle();
  wait_ms(PERIODS * (ON_MS + OFF_MS));
  TEST_ASSERT_EQUAL_INT(NODE_STATUS_OK, node_status_led_set(false));
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(0, g_subscriber.count);
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
  RUN_TEST(
      test_status_led_blinks_with_the_requested_unequal_on_and_off_times_over_several_periods);
  RUN_TEST(
      test_status_led_keeps_blinking_on_time_across_the_millisecond_clock_wrap_around);
  RUN_TEST(test_new_blink_command_restarts_the_pattern_with_the_on_phase);
  RUN_TEST(test_switching_the_status_led_on_ends_blinking);
  RUN_TEST(test_switching_the_status_led_off_ends_blinking);
  RUN_TEST(test_change_of_status_led_mode_is_logged);
  RUN_TEST(test_repeating_the_current_status_led_command_is_not_logged);
  RUN_TEST(test_individual_blink_toggles_are_not_logged);
  RUN_TEST(
      test_failing_status_led_pin_writes_are_logged_once_per_failure_streak);
  RUN_TEST(test_status_led_pin_working_again_after_failed_writes_is_logged);
  RUN_TEST(test_status_led_publishes_no_events_to_subscribers);
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

static void assert_blinks(uint32_t on_ms, uint32_t off_ms) {
  for (int period = 0; period < PERIODS; period++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(LIT_LEVEL, led_level(), "start of on phase");
    wait_ms(on_ms - 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(LIT_LEVEL, led_level(), "end of on phase");
    wait_ms(1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DARK_LEVEL, led_level(),
                                  "start of off phase");
    wait_ms(off_ms - 1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DARK_LEVEL, led_level(), "end of off phase");
    wait_ms(1);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(LIT_LEVEL, led_level(), "next period");
}

static void subscriber_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  subscriber_t *sub = (subscriber_t *)me;
  if (e->sig != NODE_AO_SIG_INIT) {
    sub->count++;
  }
}

static void start_subscriber(void) {
  const node_ao_config_t cfg = {
      .name = "subscriber",
      .priority = 1,
      .stack_bytes = 2048,
      .queue_len = SUBSCRIBER_QUEUE_LEN,
      .evt_size = NODE_AO_MAX_EVT_SIZE,
  };
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_start(&g_subscriber.super,
                                                  subscriber_dispatch, &cfg));
  /* node_dev's block is the only published one (node_ao_pub_sigs.h). */
  for (node_ao_sig_t sig = NODE_AO_PUB_SIGS_NODE_DEV;
       sig < NODE_AO_PUB_SIGS_NODE_DEV + NODE_AO_PUB_SIGS_PER_COMPONENT;
       sig++) {
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_pubsub_subscribe(&g_subscriber.super, sig));
  }
  run_until_idle();
}

static void wait_ms(uint32_t ms) {
  node_hal_time_mock_advance(ms);
  run_until_idle();
}

static int led_level(void) { return node_hal_gpio_read(LED_GPIO); }
