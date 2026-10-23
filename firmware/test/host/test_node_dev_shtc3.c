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
#include "node_dev_shtc3.h"
#include "node_dev_shtc3_internal.h"
#include "node_hal_mock.h"
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

/** @brief Events the subscriber keeps; later ones are counted only. */
#define MAX_RECORDED 32

/** @brief Events the subscriber's queue holds. */
#define SUBSCRIBER_QUEUE_LEN 16

/** @brief Fixed I2C address of the SHTC3 (datasheet). */
#define SENSOR_ADDR 0x70U

/** @brief Wakeup command (datasheet). */
#define CMD_WAKEUP 0x3517U

/** @brief Sleep command (datasheet). */
#define CMD_SLEEP 0xB098U

/** @brief Soft reset command (datasheet). */
#define CMD_SOFT_RESET 0x805DU

/** @brief Read ID register command (datasheet). */
#define CMD_READ_ID 0xEFC8U

/** @brief Measure command: normal mode, temperature first, no clock
 *         stretching (datasheet). */
#define CMD_MEASURE 0x7866U

/** @brief ID register of a real SHTC3; its product code bits are 0x0807. */
#define SHTC3_ID 0x0887U

/** @brief ID register of some other device; its product code bits are not
 *         an SHTC3's. */
#define OTHER_DEVICE_ID 0x1234U

/** @brief Wake-up time and soft reset time the driver waits, in µs
 *         (datasheet: 240 µs each). */
#define WAKE_UP_TIME_US 240U

/** @brief Bytes of one data word on the bus: MSB, LSB, CRC. */
#define WORD_SIZE 3U

/** @brief Number of I2C devices the HAL can add before its pool is full. */
#define DEVICE_POOL_SIZE 8

/** @brief Init retries that would exhaust the device pool if every retry
 *         added the sensor again. */
#define RETRIES_BEYOND_THE_DEVICE_POOL (DEVICE_POOL_SIZE + 2)

/** @brief Time between measurements of the test sensor, in ms. */
#define SAMPLE_INTERVAL_MS 10000U

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

/** @brief Data word of the datasheet's CRC example. */
static const uint8_t g_datasheet_example[] = {0xBE, 0xEF};

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
 * @brief Add an I2C bus and an SHTC3 on it to g_cfg.
 *
 * @param id Device id.
 * @return The new device's index.
 */
static int add_sensor(const char *id);

/** @brief Register the real SHTC3 driver, start the devices with g_cfg and
 *         run until idle. */
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
 * @brief Expect the next transfer to the sensor to write a command.
 *
 * @param cmd 16-bit command, sent MSB first.
 */
static void expect_command(uint16_t cmd);

/**
 * @brief Encode a data word as the sensor sends it.
 *
 * @param word The word.
 * @param out  Receives MSB, LSB and the word's CRC-8.
 */
static void encode_word(uint16_t word, uint8_t out[WORD_SIZE]);

/**
 * @brief Expect the sensor's start sequence: wakeup, soft reset, read ID
 *        answering @p id, sleep.
 *
 * @param id ID register the sensor answers with, with a correct CRC.
 */
static void expect_sensor_start(uint16_t id);

/** @brief Expect the start of a measurement: wakeup, then measure. */
static void expect_measurement_start(void);

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

/** @brief Fail the test unless every scripted I2C step was used and every
 *         transfer matched the script. */
static void assert_script_followed(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_dev_reset();
  node_dev_shtc3_reset();
  node_dev_log_mock_reset();
  g_cfg = (node_config_t){.version = 1};
  (void)memset(&g_subscriber, 0, sizeof(g_subscriber));
  start_subscriber();
}

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

/* ---- Starting the sensor ---- */

/* AC: specs/08-shtc3-driver.md #2, #4 */
void test_sensor_is_woken_reset_identified_and_put_to_sleep_at_start(void) {
  const int sensor = add_sensor("env1");
  expect_sensor_start(SHTC3_ID);
  expect_measurement_start();

  start_devices();

  assert_script_followed();
  /* Wake-up and reset at start, then wake-up for the first measurement. */
  TEST_ASSERT_EQUAL_UINT64(3U * WAKE_UP_TIME_US,
                           node_hal_time_mock_busy_wait_total_us());
  TEST_ASSERT_EQUAL_size_t(0, count_events(NODE_DEV_SIG_FAULT, sensor));
}

/* AC: specs/08-shtc3-driver.md #4 */
void test_sensor_id_is_logged_at_start(void) {
  (void)add_sensor("env1");
  expect_sensor_start(SHTC3_ID);
  expect_measurement_start();

  start_devices();

  TEST_ASSERT_EQUAL_size_t(1, node_dev_log_mock_info_count());
  TEST_ASSERT_EQUAL_STRING("[0] 'env1': SHTC3 id 0x0887",
                           node_dev_log_mock_last_info());
}

/* AC: specs/08-shtc3-driver.md #4 */
void test_sensor_that_does_not_answer_at_start_is_marked_as_faulty(void) {
  const int sensor = add_sensor("env1");
  node_hal_i2c_mock_queue_error(SENSOR_ADDR, NODE_HAL_ERR_NACK);

  start_devices();

  assert_script_followed();
  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_FAULT, sensor));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_FAULT, sensor, 0)->fault);
  TEST_ASSERT_EQUAL_size_t(0, count_events(NODE_DEV_SIG_READING, sensor));
  TEST_ASSERT_EQUAL_size_t(0, node_dev_log_mock_info_count());
}

/* AC: specs/08-shtc3-driver.md #3, #4 */
void test_sensor_with_a_corrupted_id_checksum_is_marked_as_faulty(void) {
  const int sensor = add_sensor("env1");
  uint8_t id_answer[WORD_SIZE] = {0};
  encode_word(SHTC3_ID, id_answer);
  id_answer[WORD_SIZE - 1U] ^= 0x01U;
  expect_command(CMD_WAKEUP);
  expect_command(CMD_SOFT_RESET);
  expect_command(CMD_READ_ID);
  node_hal_i2c_mock_queue_read(SENSOR_ADDR, id_answer, sizeof(id_answer));

  start_devices();

  assert_script_followed();
  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_FAULT, sensor));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_FAULT, sensor, 0)->fault);
  TEST_ASSERT_EQUAL_size_t(0, node_dev_log_mock_info_count());
}

/* AC: specs/08-shtc3-driver.md #4 */
void test_device_at_the_sensor_address_that_is_not_an_shtc3_is_marked_as_faulty(
    void) {
  const int sensor = add_sensor("env1");
  uint8_t id_answer[WORD_SIZE] = {0};
  encode_word(OTHER_DEVICE_ID, id_answer);
  expect_command(CMD_WAKEUP);
  expect_command(CMD_SOFT_RESET);
  expect_command(CMD_READ_ID);
  node_hal_i2c_mock_queue_read(SENSOR_ADDR, id_answer, sizeof(id_answer));

  start_devices();

  assert_script_followed();
  TEST_ASSERT_EQUAL_size_t(1, count_events(NODE_DEV_SIG_FAULT, sensor));
  TEST_ASSERT_TRUE(nth_event(NODE_DEV_SIG_FAULT, sensor, 0)->fault);
  TEST_ASSERT_EQUAL_size_t(0, node_dev_log_mock_info_count());
}

/* AC: specs/08-shtc3-driver.md #4 */
void test_sensor_that_keeps_failing_at_start_is_retried_without_running_out_of_bus_devices(
    void) {
  const int sensor = add_sensor("env1");
  node_hal_i2c_mock_queue_error(SENSOR_ADDR, NODE_HAL_ERR_NACK);
  start_devices();
  for (int i = 0; i < RETRIES_BEYOND_THE_DEVICE_POOL; i++) {
    node_hal_i2c_mock_queue_error(SENSOR_ADDR, NODE_HAL_ERR_NACK);
    advance_and_run(NODE_DEV_INIT_RETRY_MS);
  }
  expect_sensor_start(SHTC3_ID);
  expect_measurement_start();

  advance_and_run(NODE_DEV_INIT_RETRY_MS);

  assert_script_followed();
  TEST_ASSERT_EQUAL_size_t(2, count_events(NODE_DEV_SIG_FAULT, sensor));
  TEST_ASSERT_FALSE_MESSAGE(nth_event(NODE_DEV_SIG_FAULT, sensor, 1)->fault,
                            "the sensor recovers once it answers again");
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
  RUN_TEST(test_sensor_is_woken_reset_identified_and_put_to_sleep_at_start);
  RUN_TEST(test_sensor_id_is_logged_at_start);
  RUN_TEST(test_sensor_that_does_not_answer_at_start_is_marked_as_faulty);
  RUN_TEST(test_sensor_with_a_corrupted_id_checksum_is_marked_as_faulty);
  RUN_TEST(
      test_device_at_the_sensor_address_that_is_not_an_shtc3_is_marked_as_faulty);
  RUN_TEST(
      test_sensor_that_keeps_failing_at_start_is_retried_without_running_out_of_bus_devices);
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

static int add_sensor(const char *id) {
  TEST_ASSERT_LESS_THAN_INT(NODE_CONFIG_MAX_DEVICES, g_cfg.n_dev);
  if (g_cfg.n_i2c == 0) {
    g_cfg.i2c[0] = (node_config_i2c_bus_t){
        .id = "i2c0", .port = 0, .sda = 32, .scl = 33, .freq_hz = 100000};
    g_cfg.n_i2c = 1;
  }
  const int index = g_cfg.n_dev;
  node_config_dev_t *dev = &g_cfg.dev[index];
  (void)snprintf(dev->id, sizeof(dev->id), "%s", id);
  dev->type = NODE_CONFIG_DEV_SHTC3;
  dev->shtc3 = (node_config_shtc3_t){
      .bus_index = 0,
      .sample_interval_ms = SAMPLE_INTERVAL_MS,
      .report = {.min_interval_ms = 5000,
                 .max_interval_ms = 60000,
                 .delta_temp_c = 0.5f,
                 .delta_rh_pct = 2.0f},
  };
  g_cfg.n_dev++;
  return index;
}

static void start_devices(void) {
  TEST_ASSERT_EQUAL_INT(NODE_DEV_OK, node_dev_shtc3_register());
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

static void expect_command(uint16_t cmd) {
  const uint8_t bytes[] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFFU)};
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, bytes, sizeof(bytes));
}

static void encode_word(uint16_t word, uint8_t out[WORD_SIZE]) {
  out[0] = (uint8_t)(word >> 8);
  out[1] = (uint8_t)(word & 0xFFU);
  out[2] = node_dev_shtc3_crc8(out, 2U);
}

static void expect_sensor_start(uint16_t id) {
  uint8_t id_answer[WORD_SIZE] = {0};
  encode_word(id, id_answer);
  expect_command(CMD_WAKEUP);
  expect_command(CMD_SOFT_RESET);
  expect_command(CMD_READ_ID);
  node_hal_i2c_mock_queue_read(SENSOR_ADDR, id_answer, sizeof(id_answer));
  expect_command(CMD_SLEEP);
}

static void expect_measurement_start(void) {
  expect_command(CMD_WAKEUP);
  expect_command(CMD_MEASURE);
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

static void assert_script_followed(void) {
  TEST_ASSERT_NULL_MESSAGE(node_hal_i2c_mock_failure(),
                           node_hal_i2c_mock_failure());
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, node_hal_i2c_mock_pending_steps(),
                                   "every scripted bus step is used");
}
