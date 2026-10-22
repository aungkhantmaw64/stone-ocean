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
#include "node_dev_led.h"
#include "node_dev_log_mock.h"
#include "node_hal_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Events the subscriber keeps; later ones are counted only. */
#define MAX_RECORDED 32

/** @brief Events the subscriber's queue holds. */
#define SUBSCRIBER_QUEUE_LEN 16

/** @brief Pin of the LED, as in the example config. */
#define LED_GPIO 25

/** @brief A GPIO number the mock HAL rejects, as the ESP32 has no GPIO 40. */
#define UNUSABLE_GPIO 40

/** @brief Pin level that is high. */
#define HIGH 1

/** @brief Pin level that is low. */
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
 * @brief Add an LED to g_cfg.
 *
 * @param id          Device id.
 * @param gpio        Pin that drives the LED.
 * @param active_high True if a high level turns the LED on.
 * @param default_on  State of the LED at start.
 * @return The new device's index.
 */
static int add_led(const char *id, int gpio, bool active_high, bool default_on);

/** @brief Register the real LED driver, start the devices with g_cfg and run
 *         until idle. */
static void start_devices(void);

/** @brief Run every AO until idle; fail the test on a runaway. */
static void run_until_idle(void);

/**
 * @brief Ask the devices AO to switch an LED, as the mesh would, and run
 *        until idle.
 *
 * @param dev_index The LED's device index.
 * @param on        True to switch on.
 */
static void switch_led(int dev_index, bool on);

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

/* ---- Boot state ---- */

/* AC: specs/07-gpio-drivers.md #4 */
void test_led_is_off_after_start_when_its_config_says_default_off(void) {
  (void)add_led("led1", LED_GPIO, true, false);

  start_devices();

  TEST_ASSERT_EQUAL_INT(LOW, node_hal_gpio_read(LED_GPIO));
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_led_is_on_after_start_when_its_config_says_default_on(void) {
  (void)add_led("led1", LED_GPIO, true, true);

  start_devices();

  TEST_ASSERT_EQUAL_INT(HIGH, node_hal_gpio_read(LED_GPIO));
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_active_low_led_pin_is_driven_low_when_it_starts_on(void) {
  (void)add_led("led1", LED_GPIO, false, true);

  start_devices();

  TEST_ASSERT_EQUAL_INT(LOW, node_hal_gpio_read(LED_GPIO));
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_active_low_led_pin_is_driven_high_when_it_starts_off(void) {
  (void)add_led("led1", LED_GPIO, false, false);

  start_devices();

  TEST_ASSERT_EQUAL_INT(HIGH, node_hal_gpio_read(LED_GPIO));
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_led_boot_state_is_published_once_after_start(void) {
  const int led = add_led("led1", LED_GPIO, true, true);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_ONOFF_CHANGED, led, 0)->value.on);
}

/* AC: specs/07-gpio-drivers.md #1 */
void test_led_whose_pin_cannot_be_set_up_is_marked_as_faulty(void) {
  const int led = add_led("led1", UNUSABLE_GPIO, true, false);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_FAULT, led));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_FAULT, led, 0)->fault);
  TEST_ASSERT_EQUAL_size_t(0, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
}

/* AC: specs/07-gpio-drivers.md #1 */
void test_led_that_cannot_be_driven_at_start_is_marked_as_faulty(void) {
  const int led = add_led("led1", LED_GPIO, true, true);
  node_hal_gpio_mock_fail_next_write(NODE_HAL_ERR_IO);

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_FAULT, led));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_FAULT, led, 0)->fault);
  TEST_ASSERT_EQUAL_size_t(0, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
}

/* ---- Switching ---- */

/* AC: specs/07-gpio-drivers.md #4 */
void test_switching_an_led_on_drives_its_pin_and_publishes_the_new_state(void) {
  const int led = add_led("led1", LED_GPIO, true, false);
  start_devices();

  switch_led(led, true);

  TEST_ASSERT_EQUAL_INT(HIGH, node_hal_gpio_read(LED_GPIO));
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_ONOFF_CHANGED, led, 1)->value.on);
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_switching_an_active_low_led_on_drives_its_pin_low(void) {
  const int led = add_led("led1", LED_GPIO, false, false);
  start_devices();

  switch_led(led, true);

  TEST_ASSERT_EQUAL_INT(LOW, node_hal_gpio_read(LED_GPIO));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_ONOFF_CHANGED, led, 1)->value.on);
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_led_can_be_switched_back_off(void) {
  const int led = add_led("led1", LED_GPIO, true, false);
  start_devices();
  switch_led(led, true);

  switch_led(led, false);

  TEST_ASSERT_EQUAL_INT(LOW, node_hal_gpio_read(LED_GPIO));
  TEST_ASSERT_EQUAL_size_t(3, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
  TEST_ASSERT_FALSE(nth_event(NODE_DEV_SIG_ONOFF_CHANGED, led, 2)->value.on);
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_switching_an_led_to_its_current_state_leaves_its_pin_alone_and_publishes_nothing(
    void) {
  const int led = add_led("led1", LED_GPIO, true, true);
  start_devices();
  const size_t writes_at_start = node_hal_gpio_mock_write_count(LED_GPIO);

  switch_led(led, true);

  TEST_ASSERT_EQUAL_size_t(writes_at_start,
                           node_hal_gpio_mock_write_count(LED_GPIO));
  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
}

/* AC: specs/07-gpio-drivers.md #4 */
void test_led_keeps_its_state_when_switching_fails(void) {
  const int led = add_led("led1", LED_GPIO, true, false);
  start_devices();
  node_hal_gpio_mock_fail_next_write(NODE_HAL_ERR_IO);
  switch_led(led, true);
  const int level_after_failure = node_hal_gpio_read(LED_GPIO);
  const size_t events_after_failure =
      count_events(NODE_DEV_SIG_ONOFF_CHANGED, led);

  /* Had the failed switch been recorded as on, this would be skipped as a
   * switch to the current state. */
  switch_led(led, true);

  TEST_ASSERT_EQUAL_INT(LOW, level_after_failure);
  TEST_ASSERT_EQUAL_size_t(1, events_after_failure);
  TEST_ASSERT_EQUAL_INT(HIGH, node_hal_gpio_read(LED_GPIO));
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_ONOFF_CHANGED, led));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_led_is_off_after_start_when_its_config_says_default_off);
  RUN_TEST(test_led_is_on_after_start_when_its_config_says_default_on);
  RUN_TEST(test_active_low_led_pin_is_driven_low_when_it_starts_on);
  RUN_TEST(test_active_low_led_pin_is_driven_high_when_it_starts_off);
  RUN_TEST(test_led_boot_state_is_published_once_after_start);
  RUN_TEST(test_led_whose_pin_cannot_be_set_up_is_marked_as_faulty);
  RUN_TEST(test_led_that_cannot_be_driven_at_start_is_marked_as_faulty);
  RUN_TEST(test_switching_an_led_on_drives_its_pin_and_publishes_the_new_state);
  RUN_TEST(test_switching_an_active_low_led_on_drives_its_pin_low);
  RUN_TEST(test_led_can_be_switched_back_off);
  RUN_TEST(
      test_switching_an_led_to_its_current_state_leaves_its_pin_alone_and_publishes_nothing);
  RUN_TEST(test_led_keeps_its_state_when_switching_fails);
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

static int add_led(const char *id, int gpio, bool active_high,
                   bool default_on) {
  TEST_ASSERT_LESS_THAN_INT(NODE_CONFIG_MAX_DEVICES, g_cfg.n_dev);
  const int index = g_cfg.n_dev;
  node_config_dev_t *dev = &g_cfg.dev[index];
  (void)snprintf(dev->id, sizeof(dev->id), "%s", id);
  dev->type = NODE_CONFIG_DEV_LED;
  dev->led = (node_config_led_t){
      .gpio = gpio,
      .active_high = active_high,
      .default_on = default_on,
  };
  g_cfg.n_dev++;
  return index;
}

static void start_devices(void) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_led_register());
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_start(&g_cfg));
  run_until_idle();
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static void switch_led(int dev_index, bool on) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_set_onoff(dev_index, on));
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
