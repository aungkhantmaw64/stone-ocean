/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "node_ao.h"
#include "node_ao_port_mock.h"
#include "node_config.h"
#include "node_dev.h"
#include "node_dev_internal.h"
#include "node_dev_log_mock.h"
#include "node_dev_pir.h"
#include "node_hal_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Events the subscriber keeps; later ones are counted only. */
#define MAX_RECORDED 32

/** @brief Events the subscriber's queue holds. */
#define SUBSCRIBER_QUEUE_LEN 16

/** @brief Events the devices AO's queue holds, as in node_dev.c. */
#define DEVICES_QUEUE_LEN 16

/** @brief Debounce time of the PIRs under test, in ms. */
#define DEBOUNCE_MS 50u

/** @brief Pin of the first PIR, as in the example config. */
#define PIR_GPIO 27

/** @brief Pin of the second PIR. */
#define SECOND_PIR_GPIO 26

/** @brief A GPIO number the mock HAL rejects, as the ESP32 has no GPIO 40. */
#define UNUSABLE_GPIO 40

/** @brief Time between two edges of a bouncing or glitching output, in ms;
 *         shorter than DEBOUNCE_MS. */
#define BOUNCE_GAP_MS 10u

/** @brief Sensor output level that is high. */
#define HIGH 1

/** @brief Sensor output level that is low. */
#define LOW 0

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief A test AO that records the device events it receives. */
typedef struct {
  node_ao_t super;                     /**< Framework part; first. */
  node_dev_evt_t events[MAX_RECORDED]; /**< Received events, in order. */
  size_t count;                        /**< Events received, including
                                          ones not stored. */
} subscriber_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Config the devices are started with; rebuilt in setUp(). */
static node_config_t g_cfg = {0};

/** @brief Subscriber to every device event; reset in setUp(). */
static subscriber_t g_subscriber = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Record a device event in the subscriber.
 *
 * @param me The subscriber.
 * @param e  The event.
 */
static void subscriber_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/** @brief Start g_subscriber and subscribe it to every device event. */
static void start_subscriber(void);

/**
 * @brief Add a PIR to g_cfg.
 *
 * @param id          Device id.
 * @param gpio        Pin of the sensor output.
 * @param active_high True if a high level means motion.
 * @param pull        Internal pull resistor of the pin.
 * @return The new device's index.
 */
static int add_pir(const char *id, int gpio, bool active_high,
                   node_hal_gpio_pull_t pull);

/** @brief Register the real PIR driver, start the devices with g_cfg and run
 *         until idle. */
static void start_devices(void);

/** @brief Run every AO until idle; fail the test on a runaway. */
static void run_until_idle(void);

/**
 * @brief Move virtual time forward, then run every AO until idle.
 *
 * @param ms Time to advance, in ms.
 */
static void advance_and_run(uint32_t ms);

/**
 * @brief Drive a sensor output to a level, as the PIR module would, and run
 *        the edge's event through the devices AO.
 *
 * @param gpio  Pin of the sensor output.
 * @param level HIGH or LOW.
 */
static void drive_output(int gpio, int level);

/**
 * @brief Number of events with this signal and device the subscriber got.
 *
 * @param sig       Signal to count.
 * @param dev_index Device to count.
 * @return The count among stored events.
 */
static size_t count_events(node_ao_sig_t sig, int dev_index);

/**
 * @brief The n-th event with this signal and device the subscriber got.
 *
 * @param sig       Signal to look for.
 * @param dev_index Device to look for.
 * @param n         0 for the first matching event.
 * @return The event; fails the test if there is no such event.
 */
static const node_dev_evt_t *nth_event(node_ao_sig_t sig, int dev_index,
                                       size_t n);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_dev_reset();
  node_dev_log_mock_reset();
  g_cfg = (node_config_t){.version = 1};
  (void)memset(&g_subscriber, 0, sizeof(g_subscriber));
  start_subscriber();
}

void tearDown(void) {}

/* ---- Starting ---- */

/* AC: specs/07-gpio-drivers.md #1 */
void test_pir_reports_no_motion_once_after_start_when_its_output_is_inactive(
    void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_FALSE(nth_event(NODE_DEV_SIG_MOTION, pir, 0)->value.motion);
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_pir_reports_motion_once_after_start_when_its_output_is_already_active(
    void) {
  /* The pull-up holds the undriven output high, which means motion for an
   * active-high sensor. */
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_UP);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_MOTION, pir, 0)->value.motion);
}

/* AC: specs/07-gpio-drivers.md #1 */
void test_pir_whose_pin_cannot_be_set_up_is_marked_as_faulty(void) {
  const int pir = add_pir("pir1", UNUSABLE_GPIO, true, NODE_HAL_GPIO_PULL_NONE);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_FAULT, pir));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_FAULT, pir, 0)->fault);
  TEST_ASSERT_EQUAL_size_t(0, count_events(NODE_DEV_SIG_MOTION, pir));
}

/* ---- Debounce ---- */

/* AC: specs/07-gpio-drivers.md #2 */
void test_motion_is_reported_once_the_sensor_output_has_been_stable_for_the_debounce_time(
    void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();

  drive_output(PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS);

  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_MOTION, pir));
  const node_dev_evt_t *motion = nth_event(NODE_DEV_SIG_MOTION, pir, 1);
  TEST_ASSERT_TRUE(motion->value.motion);
  TEST_ASSERT_EQUAL_UINT32(DEBOUNCE_MS, motion->ts_ms);
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_motion_is_not_reported_before_the_debounce_time_has_passed(void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();

  drive_output(PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS - 1u);

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, pir));
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_glitch_shorter_than_the_debounce_time_is_ignored(void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();

  drive_output(PIR_GPIO, HIGH);
  advance_and_run(BOUNCE_GAP_MS);
  drive_output(PIR_GPIO, LOW);
  advance_and_run(DEBOUNCE_MS * 4u);

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_FALSE(nth_event(NODE_DEV_SIG_MOTION, pir, 0)->value.motion);
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_bouncing_sensor_output_produces_a_single_motion_event(void) {
  const int bounces = 5;
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();

  for (int i = 0; i < bounces; i++) {
    drive_output(PIR_GPIO, (i % 2 == 0) ? HIGH : LOW);
    advance_and_run(BOUNCE_GAP_MS);
  }
  const size_t events_while_bouncing = count_events(NODE_DEV_SIG_MOTION, pir);
  advance_and_run(DEBOUNCE_MS * 4u);

  TEST_ASSERT_EQUAL_size_t(1, events_while_bouncing);
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_MOTION, pir, 1)->value.motion);
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_end_of_motion_is_reported_when_the_sensor_output_goes_inactive(void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();
  drive_output(PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS);

  drive_output(PIR_GPIO, LOW);
  advance_and_run(DEBOUNCE_MS);

  TEST_ASSERT_EQUAL_size_t(3, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_FALSE(nth_event(NODE_DEV_SIG_MOTION, pir, 2)->value.motion);
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_motion_is_debounced_correctly_when_the_clock_wraps_around(void) {
  /* The edge comes 20 ms before the 32-bit ms clock wraps to 0, so the
   * debounce time ends 30 ms after the wrap. */
  const uint32_t ms_before_wrap = 20u;
  node_hal_time_mock_set(UINT32_MAX - ms_before_wrap + 1u);
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();

  drive_output(PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS - 1u);
  const size_t events_before_debounce = count_events(NODE_DEV_SIG_MOTION, pir);
  advance_and_run(1u);

  TEST_ASSERT_EQUAL_size_t(1, events_before_debounce);
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_EQUAL_UINT32(DEBOUNCE_MS - ms_before_wrap,
                           nth_event(NODE_DEV_SIG_MOTION, pir, 1)->ts_ms);
}

/* ---- Polarity ---- */

/* AC: specs/07-gpio-drivers.md #2 */
void test_active_low_pir_reports_motion_when_its_output_goes_low(void) {
  const int pir = add_pir("pir1", PIR_GPIO, false, NODE_HAL_GPIO_PULL_UP);
  start_devices();

  drive_output(PIR_GPIO, LOW);
  advance_and_run(DEBOUNCE_MS);

  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_MOTION, pir, 1)->value.motion);
}

/* AC: specs/07-gpio-drivers.md #2 */
void test_active_low_pir_reports_no_motion_at_start_when_its_output_is_high(
    void) {
  const int pir = add_pir("pir1", PIR_GPIO, false, NODE_HAL_GPIO_PULL_UP);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_FALSE(nth_event(NODE_DEV_SIG_MOTION, pir, 0)->value.motion);
}

/* ---- Interrupts ---- */

/* AC: specs/07-gpio-drivers.md #3 */
void test_sensor_interrupt_only_queues_the_edge_and_publishes_nothing_by_itself(
    void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();
  const size_t events_before = g_subscriber.count;
  const size_t logged_before = node_dev_log_mock_event_count();

  /* The mock runs the ISR synchronously, inside this call. */
  node_hal_gpio_mock_set_input(PIR_GPIO, HIGH);
  const size_t events_after_isr = g_subscriber.count;
  const size_t logged_after_isr = node_dev_log_mock_event_count();
  const int dispatched = node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(events_before, events_after_isr);
  TEST_ASSERT_EQUAL_size_t(logged_before, logged_after_isr);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, dispatched,
                                "the edge should queue exactly one event");
  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, pir));
}

/* AC: specs/07-gpio-drivers.md #3 */
void test_motion_is_still_reported_after_an_earlier_edge_was_lost_to_a_full_queue(
    void) {
  const int pir = add_pir("pir1", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();
  /* Commands the devices AO ignores, as a PIR is not an actuator; they only
   * fill its queue. */
  for (int i = 0; i < DEVICES_QUEUE_LEN; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_set_onoff(pir, true));
  }
  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_FULL, node_dev_set_onoff(pir, true));
  node_hal_gpio_mock_set_input(PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS);
  const size_t events_after_lost_edge = count_events(NODE_DEV_SIG_MOTION, pir);

  drive_output(PIR_GPIO, LOW);
  advance_and_run(DEBOUNCE_MS);
  drive_output(PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS);

  TEST_ASSERT_EQUAL_size_t(1, events_after_lost_edge);
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_MOTION, pir));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_MOTION, pir, 1)->value.motion);
}

/* ---- Several sensors ---- */

/* AC: specs/07-gpio-drivers.md #1 */
void test_two_pir_sensors_report_motion_independently_of_each_other(void) {
  const int door = add_pir("pir_door", PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  const int window =
      add_pir("pir_window", SECOND_PIR_GPIO, true, NODE_HAL_GPIO_PULL_NONE);
  start_devices();

  drive_output(SECOND_PIR_GPIO, HIGH);
  advance_and_run(DEBOUNCE_MS);

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_MOTION, door));
  TEST_ASSERT_FALSE(nth_event(NODE_DEV_SIG_MOTION, door, 0)->value.motion);
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_MOTION, window));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_MOTION, window, 1)->value.motion);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_pir_reports_no_motion_once_after_start_when_its_output_is_inactive);
  RUN_TEST(
      test_pir_reports_motion_once_after_start_when_its_output_is_already_active);
  RUN_TEST(test_pir_whose_pin_cannot_be_set_up_is_marked_as_faulty);
  RUN_TEST(
      test_motion_is_reported_once_the_sensor_output_has_been_stable_for_the_debounce_time);
  RUN_TEST(test_motion_is_not_reported_before_the_debounce_time_has_passed);
  RUN_TEST(test_glitch_shorter_than_the_debounce_time_is_ignored);
  RUN_TEST(test_bouncing_sensor_output_produces_a_single_motion_event);
  RUN_TEST(test_end_of_motion_is_reported_when_the_sensor_output_goes_inactive);
  RUN_TEST(test_motion_is_debounced_correctly_when_the_clock_wraps_around);
  RUN_TEST(test_active_low_pir_reports_motion_when_its_output_goes_low);
  RUN_TEST(
      test_active_low_pir_reports_no_motion_at_start_when_its_output_is_high);
  RUN_TEST(
      test_sensor_interrupt_only_queues_the_edge_and_publishes_nothing_by_itself);
  RUN_TEST(
      test_motion_is_still_reported_after_an_earlier_edge_was_lost_to_a_full_queue);
  RUN_TEST(test_two_pir_sensors_report_motion_independently_of_each_other);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void subscriber_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  subscriber_t *sub = (subscriber_t *)me;
  if (e->sig == NODE_AO_SIG_INIT) {
    return;
  }
  if (sub->count < MAX_RECORDED) {
    sub->events[sub->count] = *(const node_dev_evt_t *)e;
  }
  sub->count++;
}

static void start_subscriber(void) {
  const node_ao_config_t cfg = {
      .name = "subscriber",
      .priority = 1,
      .stack_bytes = 2048,
      .queue_len = SUBSCRIBER_QUEUE_LEN,
      .evt_size = sizeof(node_dev_evt_t),
  };
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_start(&g_subscriber.super,
                                                  subscriber_dispatch, &cfg));
  const node_ao_sig_t sigs[] = {NODE_DEV_SIG_READING, NODE_DEV_SIG_MOTION,
                                NODE_DEV_SIG_ONOFF_CHANGED, NODE_DEV_SIG_FAULT};
  for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
    TEST_ASSERT_EQUAL_INT(
        NODE_AO_OK, node_ao_pubsub_subscribe(&g_subscriber.super, sigs[i]));
  }
  run_until_idle();
}

static int add_pir(const char *id, int gpio, bool active_high,
                   node_hal_gpio_pull_t pull) {
  TEST_ASSERT_LESS_THAN_INT(NODE_CONFIG_MAX_DEVICES, g_cfg.n_dev);
  const int index = g_cfg.n_dev;
  node_config_dev_t *dev = &g_cfg.dev[index];
  (void)snprintf(dev->id, sizeof(dev->id), "%s", id);
  dev->type = NODE_CONFIG_DEV_PIR;
  dev->pir = (node_config_pir_t){
      .gpio = gpio,
      .active_high = active_high,
      .debounce_ms = DEBOUNCE_MS,
      .pull = pull,
  };
  g_cfg.n_dev++;
  return index;
}

static void start_devices(void) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_pir_register());
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_start(&g_cfg));
  run_until_idle();
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static void advance_and_run(uint32_t ms) {
  node_hal_time_mock_advance(ms);
  run_until_idle();
}

static void drive_output(int gpio, int level) {
  node_hal_gpio_mock_set_input(gpio, level);
  run_until_idle();
}

static size_t count_events(node_ao_sig_t sig, int dev_index) {
  size_t count = 0;
  const size_t stored =
      (g_subscriber.count < MAX_RECORDED) ? g_subscriber.count : MAX_RECORDED;
  for (size_t i = 0; i < stored; i++) {
    if ((g_subscriber.events[i].super.sig == sig) &&
        (g_subscriber.events[i].dev_index == dev_index)) {
      count++;
    }
  }
  return count;
}

static const node_dev_evt_t *nth_event(node_ao_sig_t sig, int dev_index,
                                       size_t n) {
  size_t seen = 0;
  const size_t stored =
      (g_subscriber.count < MAX_RECORDED) ? g_subscriber.count : MAX_RECORDED;
  for (size_t i = 0; i < stored; i++) {
    if ((g_subscriber.events[i].super.sig == sig) &&
        (g_subscriber.events[i].dev_index == dev_index)) {
      if (seen == n) {
        return &g_subscriber.events[i];
      }
      seen++;
    }
  }
  TEST_FAIL_MESSAGE("the subscriber did not receive the expected event");
  return NULL;
}
