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
#include "node_hal_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Events each subscriber keeps; later ones are counted only. */
#define MAX_RECORDED 32

/** @brief Events each subscriber's queue holds. */
#define SUBSCRIBER_QUEUE_LEN 16

/** @brief Time between samples of the fake sensor, in ms. */
#define SAMPLE_INTERVAL_MS 1000u

/** @brief Most drivers the framework can hold, as in node_dev.c. */
#define MAX_DRIVERS 8

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Behaviour and call record of one fake device, by device index. */
typedef struct {
  node_dev_err_t init_result;   /**< What init returns. */
  bool no_report_policy;        /**< True if init gives no report policy. */
  uint32_t sample_interval_ms;  /**< Sample interval init asks for. */
  int init_calls;               /**< Number of init calls. */
  node_hal_i2c_bus_t init_bus;  /**< Bus passed to the last init. */
  node_dev_err_t sample_result; /**< What sample returns once no pending
                                   calls are left. */
  int pending_calls;            /**< Sample calls left that return
                                   NODE_DEV_PENDING. */
  uint32_t again_ms;            /**< Delay a pending sample asks for, in
                                   ms. */
  node_dev_value_t value;       /**< Value a successful sample returns. */
  int sample_calls;             /**< Number of sample calls. */
  uint32_t last_sample_ms;      /**< HAL time of the last sample call. */
  node_dev_err_t set_result;    /**< What set_onoff returns. */
  int set_calls;                /**< Number of set_onoff calls. */
  uint32_t isr_delay_ms;        /**< Delay on_isr asks for, in ms. */
  int isr_calls;                /**< Number of on_isr calls. */
} fake_device_t;

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

/** @brief Fake devices, by device index; reset in setUp(). */
static fake_device_t g_fakes[NODE_CONFIG_MAX_DEVICES] = {0};

/** @brief Device indexes in the order their init was called. */
static int g_init_order[NODE_CONFIG_MAX_DEVICES * 4] = {0};

/** @brief Number of valid entries in g_init_order. */
static size_t g_init_count = 0;

/** @brief Number of init calls made through g_replacement_driver. */
static int g_replacement_init_calls = 0;

/** @brief Config the devices are started with; rebuilt in setUp(). */
static node_config_t g_cfg = {0};

/** @brief Two subscribers to every device event. */
static subscriber_t g_subscribers[2] = {0};

/** @brief Fake SHTC3 driver, a periodically sampled sensor; filled in by
 *         setUp(). */
static node_dev_driver_t g_sensor_driver = {0};

/** @brief Fake PIR driver, sampled after an interrupt; filled in by
 *         setUp(). */
static node_dev_driver_t g_motion_driver = {0};

/** @brief Fake LED driver, an actuator; filled in by setUp(). */
static node_dev_driver_t g_led_driver = {0};

/** @brief A second LED driver, to replace g_led_driver; filled in by
 *         setUp(). */
static node_dev_driver_t g_replacement_driver = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Fake driver init: records the call and returns the device's
 *        init_result.
 *
 * @param args  The device's index, config and bus.
 * @param state Receives the device's fake_device_t.
 * @param sched Receives the fake's interval and the config's report policy.
 * @return The device's init_result.
 */
static node_dev_err_t fake_init(const node_dev_init_args_t *args, void **state,
                                node_dev_sched_t *sched);

/**
 * @brief Fake driver sample: pending while pending_calls remain, then the
 *        device's sample_result and value.
 *
 * @param state    The device's fake_device_t.
 * @param out      Receives the value.
 * @param again_ms Receives again_ms when pending.
 * @return NODE_DEV_PENDING or the device's sample_result.
 */
static node_dev_err_t fake_sample(void *state, node_dev_value_t *out,
                                  uint32_t *again_ms);

/**
 * @brief Fake driver set_onoff: records the call and, on success, the new
 *        state as the value the next sample returns.
 *
 * @param state The device's fake_device_t.
 * @param on    Requested state.
 * @return The device's set_result.
 */
static node_dev_err_t fake_set_onoff(void *state, bool on);

/**
 * @brief Fake driver on_isr: records the call.
 *
 * @param state The device's fake_device_t.
 * @return The device's isr_delay_ms.
 */
static uint32_t fake_on_isr(void *state);

/**
 * @brief Init of the replacement driver: counts the call, then behaves as
 *        fake_init().
 *
 * @param args  As for fake_init().
 * @param state As for fake_init().
 * @param sched As for fake_init().
 * @return As for fake_init().
 */
static node_dev_err_t replacement_init(const node_dev_init_args_t *args,
                                       void **state, node_dev_sched_t *sched);

/**
 * @brief Record a device event in the subscriber.
 *
 * @param me The subscriber.
 * @param e  The event.
 */
static void subscriber_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Start a subscriber and subscribe it to every device event.
 *
 * @param sub Subscriber to start.
 */
static void start_subscriber(subscriber_t *sub);

/**
 * @brief Add a device to g_cfg.
 *
 * @param type Device type.
 * @param id   Device id.
 * @return The new device's index.
 */
static int add_device(node_config_dev_type_t type, const char *id);

/** @brief Fill in the fake drivers' method tables. */
static void make_fake_drivers(void);

/** @brief Register the fake sensor, motion and LED drivers. */
static void register_fake_drivers(void);

/** @brief Start the devices with g_cfg and run until idle. */
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
 * @brief Number of events with this signal and device a subscriber got.
 *
 * @param sub       The subscriber.
 * @param sig       Signal to count.
 * @param dev_index Device to count.
 * @return The count among stored events.
 */
static size_t count_events(const subscriber_t *sub, node_ao_sig_t sig,
                           int dev_index);

/**
 * @brief The n-th event with this signal and device a subscriber got.
 *
 * @param sub       The subscriber.
 * @param sig       Signal to look for.
 * @param dev_index Device to look for.
 * @param n         0 for the first matching event.
 * @return The event; fails the test if there is no such event.
 */
static const node_dev_evt_t *nth_event(const subscriber_t *sub,
                                       node_ao_sig_t sig, int dev_index,
                                       size_t n);

/**
 * @brief Advance time by one sample interval, @p count times, running every
 *        AO after each step.
 *
 * @param count Number of sample intervals.
 */
static void run_sample_intervals(int count);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_dev_reset();
  node_dev_log_mock_reset();
  (void)memset(g_fakes, 0, sizeof(g_fakes));
  for (size_t i = 0; i < NODE_CONFIG_MAX_DEVICES; i++) {
    g_fakes[i].sample_interval_ms = SAMPLE_INTERVAL_MS;
  }
  (void)memset(g_init_order, 0, sizeof(g_init_order));
  g_init_count = 0;
  g_replacement_init_calls = 0;
  (void)memset(g_subscribers, 0, sizeof(g_subscribers));
  make_fake_drivers();

  g_cfg = (node_config_t){.version = 1, .n_i2c = 1};
  (void)snprintf(g_cfg.i2c[0].id, sizeof(g_cfg.i2c[0].id), "%s", "i2c0");
  g_cfg.i2c[0].sda = 21;
  g_cfg.i2c[0].scl = 22;
  g_cfg.i2c[0].freq_hz = 100000u;

  start_subscriber(&g_subscribers[0]);
  start_subscriber(&g_subscribers[1]);
}

void tearDown(void) {}

/* ---- Registering drivers ---- */

/* AC: specs/06-driver-framework.md #1 */
void test_driver_without_an_init_method_is_rejected(void) {
  const node_dev_driver_t driver = {.type = NODE_CONFIG_DEV_LED,
                                    .sample_sig = NODE_DEV_SIG_ONOFF_CHANGED};

  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_ARG, node_dev_register_driver(&driver));
}

/* AC: specs/06-driver-framework.md #1 */
void test_driver_with_an_unsupported_event_kind_is_rejected(void) {
  const node_dev_driver_t driver = {.type = NODE_CONFIG_DEV_LED,
                                    .sample_sig = NODE_DEV_SIG_FAULT,
                                    .init = fake_init};

  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_ARG, node_dev_register_driver(&driver));
}

/* AC: specs/06-driver-framework.md #1 */
void test_registering_a_driver_after_the_devices_have_started_is_rejected(
    void) {
  (void)add_device(NODE_CONFIG_DEV_LED, "led1");
  start_devices();

  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_STATE,
                        node_dev_register_driver(&g_led_driver));
}

/* AC: specs/06-driver-framework.md #1 */
void test_registering_a_second_driver_for_the_same_device_type_replaces_the_first(
    void) {
  const int led = add_device(NODE_CONFIG_DEV_LED, "led1");
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_register_driver(&g_led_driver));

  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK,
                        node_dev_register_driver(&g_replacement_driver));
  start_devices();

  TEST_ASSERT_EQUAL_INT(1, g_replacement_init_calls);
  TEST_ASSERT_EQUAL_INT(1, g_fakes[led].init_calls);
}

/* AC: specs/06-driver-framework.md #1 */
void test_ninth_driver_type_is_rejected_when_the_registry_is_full(void) {
  static node_dev_driver_t drivers[MAX_DRIVERS + 1] = {0};
  for (int i = 0; i <= MAX_DRIVERS; i++) {
    drivers[i] = g_sensor_driver;
    /* Types beyond the three real ones stand in for future device types. */
    drivers[i].type = (node_config_dev_type_t)i;
  }
  for (int i = 0; i < MAX_DRIVERS; i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_DEV_OK,
                                  node_dev_register_driver(&drivers[i]),
                                  "registering one of the first eight types");
  }

  const node_dev_err_t err = node_dev_register_driver(&drivers[MAX_DRIVERS]);

  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_FULL, err);
}

/* ---- Starting the devices ---- */

/* AC: specs/06-driver-framework.md #1 */
void test_devices_cannot_be_started_without_a_config(void) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_ARG, node_dev_start(NULL));
}

/* AC: specs/06-driver-framework.md #1 */
void test_devices_cannot_be_started_twice(void) {
  (void)add_device(NODE_CONFIG_DEV_LED, "led1");
  register_fake_drivers();
  start_devices();

  TEST_ASSERT_EQUAL_INT(NODE_DEV_ERR_STATE, node_dev_start(&g_cfg));
  run_until_idle();
  TEST_ASSERT_EQUAL_INT(1, g_fakes[0].init_calls);
}

/* AC: specs/06-driver-framework.md #1 */
void test_devices_are_initialised_in_config_order(void) {
  (void)add_device(NODE_CONFIG_DEV_LED, "led1");
  (void)add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  (void)add_device(NODE_CONFIG_DEV_PIR, "pir1");
  (void)add_device(NODE_CONFIG_DEV_LED, "led2");
  register_fake_drivers();

  start_devices();

  const int expected[] = {0, 1, 2, 3};
  TEST_ASSERT_EQUAL_size_t(4, g_init_count);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, g_init_order, 4);
}

/* AC: specs/06-driver-framework.md #1 */
void test_sensor_receives_its_i2c_bus_already_set_up(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  const int led = add_device(NODE_CONFIG_DEV_LED, "led1");
  register_fake_drivers();

  start_devices();

  TEST_ASSERT_NOT_NULL(g_fakes[env].init_bus);
  TEST_ASSERT_NULL(g_fakes[led].init_bus);
}

/* AC: specs/06-driver-framework.md #6 */
void test_device_table_lists_every_device_with_its_state_at_start(void) {
  const int led = add_device(NODE_CONFIG_DEV_LED, "led1");
  const int pir = add_device(NODE_CONFIG_DEV_PIR, "pir1");
  g_fakes[pir].init_result = NODE_DEV_ERR_IO;
  register_fake_drivers();

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, node_dev_log_mock_table_count());
  TEST_ASSERT_EQUAL_size_t(2, node_dev_log_mock_row_count());
  TEST_ASSERT_EQUAL_INT(led, node_dev_log_mock_row(0)->dev_index);
  TEST_ASSERT_FALSE(node_dev_log_mock_row(0)->fault);
  TEST_ASSERT_EQUAL_INT(pir, node_dev_log_mock_row(1)->dev_index);
  TEST_ASSERT_TRUE(node_dev_log_mock_row(1)->fault);
}

/* AC: specs/06-driver-framework.md #3 */
void test_device_with_no_driver_for_its_type_is_marked_as_faulty_and_never_retried(
    void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_register_driver(&g_led_driver));

  start_devices();
  node_hal_time_mock_advance(2u * NODE_DEV_INIT_RETRY_MS);
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_TRUE(g_subscribers[0].events[0].fault);
  TEST_ASSERT_EQUAL_size_t(1, node_dev_log_mock_state_change_count());
  TEST_ASSERT_EQUAL_INT(0, g_fakes[env].init_calls);
}

/* AC: specs/06-driver-framework.md #3 */
void test_sensor_whose_driver_gives_no_report_policy_is_marked_as_faulty(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].no_report_policy = true;
  register_fake_drivers();

  start_devices();

  TEST_ASSERT_TRUE(node_dev_log_mock_row(0)->fault);
  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_EQUAL_INT(0, g_fakes[env].sample_calls);
}

/* ---- Sampling and publishing ---- */

/* AC: specs/06-driver-framework.md #2 */
void test_sensor_is_sampled_right_after_start_and_then_every_sample_interval(
    void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();

  start_devices();
  TEST_ASSERT_EQUAL_INT(1, g_fakes[env].sample_calls);

  advance_and_run(SAMPLE_INTERVAL_MS - 1u);
  TEST_ASSERT_EQUAL_INT(1, g_fakes[env].sample_calls);

  advance_and_run(1u);
  TEST_ASSERT_EQUAL_INT(2, g_fakes[env].sample_calls);

  advance_and_run(SAMPLE_INTERVAL_MS);
  TEST_ASSERT_EQUAL_INT(3, g_fakes[env].sample_calls);
  TEST_ASSERT_EQUAL_UINT32(2u * SAMPLE_INTERVAL_MS,
                           g_fakes[env].last_sample_ms);
}

/* AC: specs/06-driver-framework.md #2 */
void test_readings_are_throttled_by_the_report_policy_while_the_node_runs(
    void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].value = (node_dev_value_t){.temp_c = 20.0f, .rh_pct = 40.0f};
  register_fake_drivers();
  start_devices();

  /* A 1 °C rise, above the 0.5 °C delta, is sampled every second but held
   * back until the 5 s minimum interval has passed. */
  g_fakes[env].value.temp_c = 21.0f;
  for (int second = 1; second <= 4; second++) {
    advance_and_run(SAMPLE_INTERVAL_MS);
  }
  const size_t before_minimum =
      count_events(&g_subscribers[0], NODE_DEV_SIG_READING, env);
  advance_and_run(SAMPLE_INTERVAL_MS);
  const size_t at_minimum =
      count_events(&g_subscribers[0], NODE_DEV_SIG_READING, env);

  TEST_ASSERT_EQUAL_size_t(1, before_minimum);
  TEST_ASSERT_EQUAL_size_t(2, at_minimum);
  TEST_ASSERT_EQUAL_FLOAT(21.0f, g_subscribers[0].events[1].value.temp_c);
  TEST_ASSERT_EQUAL_UINT32(5u * SAMPLE_INTERVAL_MS,
                           g_subscribers[0].events[1].ts_ms);
}

/* AC: specs/06-driver-framework.md #2 */
void test_published_reading_reaches_every_subscriber(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].value = (node_dev_value_t){.temp_c = 22.5f, .rh_pct = 55.0f};
  register_fake_drivers();

  start_devices();

  for (size_t i = 0; i < 2; i++) {
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, g_subscribers[i].count,
                                     "events received by a subscriber");
    const node_dev_evt_t *evt = &g_subscribers[i].events[0];
    TEST_ASSERT_EQUAL_UINT16(NODE_DEV_SIG_READING, evt->super.sig);
    TEST_ASSERT_EQUAL_UINT8(env, evt->dev_index);
    TEST_ASSERT_EQUAL_FLOAT(22.5f, evt->value.temp_c);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, evt->value.rh_pct);
  }
}

/* AC: specs/06-driver-framework.md #6 */
void test_every_published_reading_is_logged(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].value = (node_dev_value_t){.temp_c = 20.0f, .rh_pct = 40.0f};
  register_fake_drivers();
  start_devices();

  g_fakes[env].value.temp_c = 25.0f;
  for (int second = 1; second <= 5; second++) {
    advance_and_run(SAMPLE_INTERVAL_MS);
  }

  const size_t published =
      count_events(&g_subscribers[0], NODE_DEV_SIG_READING, env);
  TEST_ASSERT_EQUAL_size_t(2, published);
  TEST_ASSERT_EQUAL_size_t(published, node_dev_log_mock_event_count());
  TEST_ASSERT_EQUAL_FLOAT(25.0f, node_dev_log_mock_event(1)->value.temp_c);
}

/* AC: specs/06-driver-framework.md #5 */
void test_sample_still_in_progress_is_finished_after_the_time_the_driver_asked_for(
    void) {
  /* The SHTC3 needs about 12 ms to measure; the fake asks for 15 ms. */
  const uint32_t measure_ms = 15u;
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].pending_calls = 1;
  g_fakes[env].again_ms = measure_ms;
  register_fake_drivers();

  start_devices();
  advance_and_run(measure_ms - 1u);
  const int calls_before = g_fakes[env].sample_calls;
  advance_and_run(1u);

  TEST_ASSERT_EQUAL_INT(1, calls_before);
  TEST_ASSERT_EQUAL_INT(2, g_fakes[env].sample_calls);
  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_READING, env));
  TEST_ASSERT_EQUAL_UINT32(measure_ms, g_subscribers[0].events[0].ts_ms);
}

/* AC: specs/06-driver-framework.md #5 */
void test_sample_in_progress_does_not_delay_the_next_periodic_sample(void) {
  const uint32_t measure_ms = 15u;
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].pending_calls = 1;
  g_fakes[env].again_ms = measure_ms;
  register_fake_drivers();
  start_devices();
  advance_and_run(measure_ms);

  advance_and_run(SAMPLE_INTERVAL_MS - measure_ms);

  TEST_ASSERT_EQUAL_INT(3, g_fakes[env].sample_calls);
  TEST_ASSERT_EQUAL_UINT32(SAMPLE_INTERVAL_MS, g_fakes[env].last_sample_ms);
}

/* AC: specs/06-driver-framework.md #2 */
void test_led_initial_state_is_published_once_after_start(void) {
  const int led = add_device(NODE_CONFIG_DEV_LED, "led1");
  g_fakes[led].value.on = true;
  register_fake_drivers();

  start_devices();
  advance_and_run(10u * SAMPLE_INTERVAL_MS);

  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_ONOFF_CHANGED, led));
  TEST_ASSERT_TRUE(g_subscribers[0].events[0].value.on);
  TEST_ASSERT_EQUAL_INT(1, g_fakes[led].sample_calls);
}

/* ---- Faults and recovery ---- */

/* AC: specs/06-driver-framework.md #3 */
void test_two_failed_samples_in_a_row_do_not_mark_a_sensor_as_faulty(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();
  start_devices();

  /* Two failures, one good sample, two more failures: four failures in
   * all, but never three in a row. */
  g_fakes[env].sample_result = NODE_DEV_ERR_IO;
  run_sample_intervals(2);
  g_fakes[env].sample_result = NODE_DEV_OK;
  run_sample_intervals(1);
  g_fakes[env].sample_result = NODE_DEV_ERR_IO;
  run_sample_intervals(2);

  TEST_ASSERT_EQUAL_size_t(
      0, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_EQUAL_size_t(0, node_dev_log_mock_state_change_count());
}

/* AC: specs/06-driver-framework.md #3 */
void test_sensor_is_marked_as_faulty_after_three_failed_samples_in_a_row(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();
  start_devices();

  g_fakes[env].sample_result = NODE_DEV_ERR_IO;
  run_sample_intervals(NODE_DEV_MAX_SAMPLE_FAILS);

  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_TRUE(
      nth_event(&g_subscribers[0], NODE_DEV_SIG_FAULT, env, 0)->fault);
  /* A faulty sensor keeps being sampled on its schedule. */
  run_sample_intervals(1);
  TEST_ASSERT_EQUAL_INT(1 + NODE_DEV_MAX_SAMPLE_FAILS + 1,
                        g_fakes[env].sample_calls);
}

/* AC: specs/06-driver-framework.md #3 */
void test_faulty_sensor_recovers_on_its_next_successful_sample(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();
  start_devices();
  g_fakes[env].sample_result = NODE_DEV_ERR_IO;
  run_sample_intervals(NODE_DEV_MAX_SAMPLE_FAILS);

  g_fakes[env].sample_result = NODE_DEV_OK;
  run_sample_intervals(1);

  TEST_ASSERT_EQUAL_size_t(
      2, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_FALSE(
      nth_event(&g_subscribers[0], NODE_DEV_SIG_FAULT, env, 1)->fault);
}

/* AC: specs/06-driver-framework.md #3 */
void test_fault_and_recovery_are_both_published_to_subscribers(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();
  start_devices();

  g_fakes[env].sample_result = NODE_DEV_ERR_IO;
  run_sample_intervals(NODE_DEV_MAX_SAMPLE_FAILS);
  g_fakes[env].sample_result = NODE_DEV_OK;
  run_sample_intervals(1);

  for (size_t i = 0; i < 2; i++) {
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        2, count_events(&g_subscribers[i], NODE_DEV_SIG_FAULT, env),
        "fault events received by a subscriber");
    TEST_ASSERT_TRUE(
        nth_event(&g_subscribers[i], NODE_DEV_SIG_FAULT, env, 0)->fault);
    TEST_ASSERT_FALSE(
        nth_event(&g_subscribers[i], NODE_DEV_SIG_FAULT, env, 1)->fault);
  }
}

/* AC: specs/06-driver-framework.md #6 */
void test_every_state_change_is_logged(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();
  start_devices();

  g_fakes[env].sample_result = NODE_DEV_ERR_IO;
  run_sample_intervals(NODE_DEV_MAX_SAMPLE_FAILS);
  g_fakes[env].sample_result = NODE_DEV_OK;
  run_sample_intervals(1);

  TEST_ASSERT_EQUAL_size_t(2, node_dev_log_mock_state_change_count());
  TEST_ASSERT_EQUAL_INT(env, node_dev_log_mock_state_change(0)->dev_index);
  TEST_ASSERT_TRUE(node_dev_log_mock_state_change(0)->fault);
  TEST_ASSERT_EQUAL_INT(env, node_dev_log_mock_state_change(1)->dev_index);
  TEST_ASSERT_FALSE(node_dev_log_mock_state_change(1)->fault);
}

/* AC: specs/06-driver-framework.md #3 */
void test_device_that_fails_to_initialise_is_retried_every_30_seconds(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].init_result = NODE_DEV_ERR_IO;
  register_fake_drivers();
  start_devices();

  advance_and_run(NODE_DEV_INIT_RETRY_MS - 1u);
  const int calls_before_retry = g_fakes[env].init_calls;
  advance_and_run(1u);
  const int calls_at_first_retry = g_fakes[env].init_calls;
  advance_and_run(NODE_DEV_INIT_RETRY_MS);

  TEST_ASSERT_EQUAL_INT(1, calls_before_retry);
  TEST_ASSERT_EQUAL_INT(2, calls_at_first_retry);
  TEST_ASSERT_EQUAL_INT(3, g_fakes[env].init_calls);
  TEST_ASSERT_EQUAL_INT(0, g_fakes[env].sample_calls);
}

/* AC: specs/06-driver-framework.md #3 */
void test_device_recovers_when_an_init_retry_succeeds(void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  g_fakes[env].init_result = NODE_DEV_ERR_IO;
  register_fake_drivers();
  start_devices();

  g_fakes[env].init_result = NODE_DEV_OK;
  advance_and_run(NODE_DEV_INIT_RETRY_MS);

  TEST_ASSERT_EQUAL_size_t(
      2, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_FALSE(
      nth_event(&g_subscribers[0], NODE_DEV_SIG_FAULT, env, 1)->fault);
  TEST_ASSERT_EQUAL_INT(1, g_fakes[env].sample_calls);
  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_READING, env));
}

/* AC: specs/06-driver-framework.md #3 */
void test_faulty_device_does_not_stop_other_devices_from_being_sampled(void) {
  const int broken = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  const int working = add_device(NODE_CONFIG_DEV_SHTC3, "env2");
  g_fakes[broken].init_result = NODE_DEV_ERR_IO;
  register_fake_drivers();

  start_devices();
  run_sample_intervals(3);

  TEST_ASSERT_EQUAL_INT(0, g_fakes[broken].sample_calls);
  TEST_ASSERT_EQUAL_INT(4, g_fakes[working].sample_calls);
  TEST_ASSERT_EQUAL_size_t(
      0, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, working));
  TEST_ASSERT_EQUAL_size_t(
      1, count_events(&g_subscribers[0], NODE_DEV_SIG_READING, working));
}

/* AC: specs/06-driver-framework.md #3 */
void test_device_on_a_failed_i2c_bus_is_retried_once_the_bus_can_be_set_up_again(
    void) {
  const int env = add_device(NODE_CONFIG_DEV_SHTC3, "env1");
  register_fake_drivers();
  node_hal_i2c_mock_fail_next_bus_init(NODE_HAL_ERR_IO);

  start_devices();
  const int calls_while_bus_down = g_fakes[env].init_calls;
  advance_and_run(NODE_DEV_INIT_RETRY_MS);

  TEST_ASSERT_EQUAL_INT(0, calls_while_bus_down);
  TEST_ASSERT_EQUAL_INT(1, g_fakes[env].init_calls);
  TEST_ASSERT_NOT_NULL(g_fakes[env].init_bus);
  TEST_ASSERT_EQUAL_size_t(
      2, count_events(&g_subscribers[0], NODE_DEV_SIG_FAULT, env));
  TEST_ASSERT_FALSE(
      nth_event(&g_subscribers[0], NODE_DEV_SIG_FAULT, env, 1)->fault);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_driver_without_an_init_method_is_rejected);
  RUN_TEST(test_driver_with_an_unsupported_event_kind_is_rejected);
  RUN_TEST(
      test_registering_a_driver_after_the_devices_have_started_is_rejected);
  RUN_TEST(
      test_registering_a_second_driver_for_the_same_device_type_replaces_the_first);
  RUN_TEST(test_ninth_driver_type_is_rejected_when_the_registry_is_full);
  RUN_TEST(test_devices_cannot_be_started_without_a_config);
  RUN_TEST(test_devices_cannot_be_started_twice);
  RUN_TEST(test_devices_are_initialised_in_config_order);
  RUN_TEST(test_sensor_receives_its_i2c_bus_already_set_up);
  RUN_TEST(test_device_table_lists_every_device_with_its_state_at_start);
  RUN_TEST(
      test_device_with_no_driver_for_its_type_is_marked_as_faulty_and_never_retried);
  RUN_TEST(test_sensor_whose_driver_gives_no_report_policy_is_marked_as_faulty);
  RUN_TEST(
      test_sensor_is_sampled_right_after_start_and_then_every_sample_interval);
  RUN_TEST(
      test_readings_are_throttled_by_the_report_policy_while_the_node_runs);
  RUN_TEST(test_published_reading_reaches_every_subscriber);
  RUN_TEST(test_every_published_reading_is_logged);
  RUN_TEST(
      test_sample_still_in_progress_is_finished_after_the_time_the_driver_asked_for);
  RUN_TEST(test_sample_in_progress_does_not_delay_the_next_periodic_sample);
  RUN_TEST(test_led_initial_state_is_published_once_after_start);
  RUN_TEST(test_two_failed_samples_in_a_row_do_not_mark_a_sensor_as_faulty);
  RUN_TEST(test_sensor_is_marked_as_faulty_after_three_failed_samples_in_a_row);
  RUN_TEST(test_faulty_sensor_recovers_on_its_next_successful_sample);
  RUN_TEST(test_fault_and_recovery_are_both_published_to_subscribers);
  RUN_TEST(test_every_state_change_is_logged);
  RUN_TEST(test_device_that_fails_to_initialise_is_retried_every_30_seconds);
  RUN_TEST(test_device_recovers_when_an_init_retry_succeeds);
  RUN_TEST(test_faulty_device_does_not_stop_other_devices_from_being_sampled);
  RUN_TEST(
      test_device_on_a_failed_i2c_bus_is_retried_once_the_bus_can_be_set_up_again);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_dev_err_t fake_init(const node_dev_init_args_t *args, void **state,
                                node_dev_sched_t *sched) {
  fake_device_t *fake = &g_fakes[args->dev_index];
  fake->init_calls++;
  fake->init_bus = args->bus;
  if (g_init_count < (sizeof(g_init_order) / sizeof(g_init_order[0]))) {
    g_init_order[g_init_count] = args->dev_index;
    g_init_count++;
  }
  *state = fake;
  if (args->cfg->type == NODE_CONFIG_DEV_SHTC3) {
    sched->sample_interval_ms = fake->sample_interval_ms;
    if (!fake->no_report_policy) {
      sched->report = &args->cfg->shtc3.report;
    }
  }
  return fake->init_result;
}

static node_dev_err_t fake_sample(void *state, node_dev_value_t *out,
                                  uint32_t *again_ms) {
  fake_device_t *fake = state;
  fake->sample_calls++;
  fake->last_sample_ms = node_hal_time_ms();
  if (fake->pending_calls > 0) {
    fake->pending_calls--;
    *again_ms = fake->again_ms;
    return NODE_DEV_PENDING;
  }
  *out = fake->value;
  return fake->sample_result;
}

static node_dev_err_t fake_set_onoff(void *state, bool on) {
  fake_device_t *fake = state;
  fake->set_calls++;
  if (fake->set_result == NODE_DEV_OK) {
    fake->value.on = on;
  }
  return fake->set_result;
}

static uint32_t fake_on_isr(void *state) {
  fake_device_t *fake = state;
  fake->isr_calls++;
  return fake->isr_delay_ms;
}

static node_dev_err_t replacement_init(const node_dev_init_args_t *args,
                                       void **state, node_dev_sched_t *sched) {
  g_replacement_init_calls++;
  return fake_init(args, state, sched);
}

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

static void start_subscriber(subscriber_t *sub) {
  const node_ao_config_t cfg = {
      .name = "subscriber",
      .priority = 1,
      .stack_bytes = 2048,
      .queue_len = SUBSCRIBER_QUEUE_LEN,
      .evt_size = sizeof(node_dev_evt_t),
  };
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_start(&sub->super, subscriber_dispatch, &cfg));
  const node_ao_sig_t sigs[] = {NODE_DEV_SIG_READING, NODE_DEV_SIG_MOTION,
                                NODE_DEV_SIG_ONOFF_CHANGED, NODE_DEV_SIG_FAULT};
  for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_pubsub_subscribe(&sub->super, sigs[i]));
  }
  run_until_idle();
}

static int add_device(node_config_dev_type_t type, const char *id) {
  TEST_ASSERT_LESS_THAN_INT(NODE_CONFIG_MAX_DEVICES, g_cfg.n_dev);
  const int index = g_cfg.n_dev;
  node_config_dev_t *dev = &g_cfg.dev[index];
  (void)snprintf(dev->id, sizeof(dev->id), "%s", id);
  dev->type = type;
  if (type == NODE_CONFIG_DEV_SHTC3) {
    dev->shtc3.bus_index = 0;
    dev->shtc3.sample_interval_ms = SAMPLE_INTERVAL_MS;
    dev->shtc3.report = (node_config_report_t){
        .min_interval_ms = 5000u,
        .max_interval_ms = 60000u,
        .delta_temp_c = 0.5f,
        .delta_rh_pct = 2.0f,
    };
  }
  g_cfg.n_dev++;
  return index;
}

static void make_fake_drivers(void) {
  g_sensor_driver = (node_dev_driver_t){
      .type = NODE_CONFIG_DEV_SHTC3,
      .sample_sig = NODE_DEV_SIG_READING,
      .init = fake_init,
      .sample = fake_sample,
  };
  g_motion_driver = (node_dev_driver_t){
      .type = NODE_CONFIG_DEV_PIR,
      .sample_sig = NODE_DEV_SIG_MOTION,
      .init = fake_init,
      .sample = fake_sample,
      .on_isr = fake_on_isr,
  };
  g_led_driver = (node_dev_driver_t){
      .type = NODE_CONFIG_DEV_LED,
      .sample_sig = NODE_DEV_SIG_ONOFF_CHANGED,
      .init = fake_init,
      .sample = fake_sample,
      .set_onoff = fake_set_onoff,
  };
  g_replacement_driver = g_led_driver;
  g_replacement_driver.init = replacement_init;
}

static void register_fake_drivers(void) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK,
                        node_dev_register_driver(&g_sensor_driver));
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK,
                        node_dev_register_driver(&g_motion_driver));
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_register_driver(&g_led_driver));
}

static void start_devices(void) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_start(&g_cfg));
  run_until_idle();
}

static void advance_and_run(uint32_t ms) {
  node_hal_time_mock_advance(ms);
  run_until_idle();
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static size_t count_events(const subscriber_t *sub, node_ao_sig_t sig,
                           int dev_index) {
  size_t count = 0;
  const size_t stored = (sub->count < MAX_RECORDED) ? sub->count : MAX_RECORDED;
  for (size_t i = 0; i < stored; i++) {
    if ((sub->events[i].super.sig == sig) &&
        (sub->events[i].dev_index == dev_index)) {
      count++;
    }
  }
  return count;
}

static const node_dev_evt_t *nth_event(const subscriber_t *sub,
                                       node_ao_sig_t sig, int dev_index,
                                       size_t n) {
  size_t seen = 0;
  const size_t stored = (sub->count < MAX_RECORDED) ? sub->count : MAX_RECORDED;
  for (size_t i = 0; i < stored; i++) {
    if ((sub->events[i].super.sig == sig) &&
        (sub->events[i].dev_index == dev_index)) {
      if (seen == n) {
        return &sub->events[i];
      }
      seen++;
    }
  }
  TEST_FAIL_MESSAGE("the subscriber did not receive the expected event");
  return NULL;
}

static void run_sample_intervals(int count) {
  for (int i = 0; i < count; i++) {
    advance_and_run(SAMPLE_INTERVAL_MS);
  }
}
