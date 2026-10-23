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
#include "node_dev_mock.h"
#include "node_hal_mock.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"
#include "node_mesh_log_mock.h"
#include "node_mesh_models.h"
#include "node_mesh_models_port_mock.h"
#include "node_mesh_msg.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Device index of the temperature and humidity sensor in g_config. */
#define SENSOR 0

/** @brief Device index of the motion sensor in g_config. */
#define MOTION_SENSOR 1

/** @brief Device index of the LED in g_config. */
#define LED 2

/** @brief A device index the three-device g_config does not have. */
#define MISSING_DEVICE 5

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Active config of these tests: a sensor, a motion sensor and an LED,
 *         in that order. Reset in setUp(). */
static node_config_t g_config = {0};

/** @brief A sensor reading of 23.45 °C and 45.67 %. */
static const node_dev_value_t g_reading = {.temp_c = 23.45f, .rh_pct = 45.67f};

/** @brief DeviceStatus of the sensor (device 0) with g_reading: 235 and 457
 *         tenths. */
static const uint8_t g_reading_status[] = {0x10, 0xd6, 0x03, 0x18, 0xc9, 0x03};

/** @brief DeviceStatus of the motion sensor (device 1) sensing motion. */
static const uint8_t g_motion_status[] = {0x08, 0x01, 0x20, 0x01};

/** @brief DeviceStatus of the LED (device 2) switched on. */
static const uint8_t g_led_on_status[] = {0x08, 0x02, 0x28, 0x01};

/** @brief DeviceStatus of the sensor (device 0) in FAULT: the fault flag and
 *         no values. */
static const uint8_t g_sensor_fault_status[] = {0x30, 0x01};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Start the models AO with g_config and run its init event. Fails the
 *        test if it does not start.
 */
static void start_models_task(void);

/**
 * @brief Publish a device event as the devices AO would, and run the AOs
 *        until idle.
 *
 * @param sig       One of NODE_DEV_SIG_*.
 * @param dev_index Device index.
 * @param fault     For NODE_DEV_SIG_FAULT, the new state.
 * @param value     The device's value; zero for NODE_DEV_SIG_FAULT.
 */
static void device_reports(node_ao_sig_t sig, int dev_index, bool fault,
                           node_dev_value_t value);

/**
 * @brief Check that the last publication is a DeviceStatus with the given
 *        payload.
 *
 * @param expected Expected payload; may be NULL if @p len is 0.
 * @param len      Its length in bytes.
 */
static void assert_last_published_status(const uint8_t *expected, size_t len);

/**
 * @brief Dispatch queued events until no AO has any left. Fails the test if
 *        the AOs never go idle.
 */
static void run_until_idle(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_mesh_models_reset();
  node_mesh_models_port_mock_reset();
  node_mesh_log_mock_reset();
  node_dev_mock_reset();
  (void)memset(&g_config, 0, sizeof(g_config));
  g_config.version = 1;
  g_config.n_dev = 3;
  g_config.dev[SENSOR].type = NODE_CONFIG_DEV_SHTC3;
  (void)snprintf(g_config.dev[SENSOR].id, NODE_CONFIG_ID_LEN, "env1");
  g_config.dev[MOTION_SENSOR].type = NODE_CONFIG_DEV_PIR;
  (void)snprintf(g_config.dev[MOTION_SENSOR].id, NODE_CONFIG_ID_LEN, "pir1");
  g_config.dev[LED].type = NODE_CONFIG_DEV_LED;
  (void)snprintf(g_config.dev[LED].id, NODE_CONFIG_ID_LEN, "led1");
}

void tearDown(void) {}

/* ---- Starting ---- */

/* AC: specs/11-mesh-model-mapping.md #1 */
void test_models_task_registers_the_vendor_model_when_it_starts(void) {
  node_mesh_err_t err = node_mesh_models_start(&g_config);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_register_count());
}

void test_models_task_cannot_be_started_twice(void) {
  start_models_task();

  node_mesh_err_t err = node_mesh_models_start(&g_config);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_STATE, err);
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_register_count());
}

void test_config_with_more_devices_than_allowed_is_rejected_at_start(void) {
  g_config.n_dev = NODE_CONFIG_MAX_DEVICES + 1;

  node_mesh_err_t err = node_mesh_models_start(&g_config);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_register_count());
}

void test_start_failure_is_reported_when_the_vendor_model_cannot_be_registered(
    void) {
  /* node_mesh refuses vendor models once the mesh has started. */
  node_mesh_models_port_mock_set_register_result(NODE_MESH_ERR_STATE);

  node_mesh_err_t err = node_mesh_models_start(&g_config);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_STATE, err);
}

/* ---- Publishing ---- */

/* AC: specs/11-mesh-model-mapping.md #6 */
void test_sensor_reading_is_published_when_the_gateway_has_set_a_publish_address(
    void) {
  start_models_task();

  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_publish_count());
  assert_last_published_status(g_reading_status, sizeof(g_reading_status));
}

/* AC: specs/11-mesh-model-mapping.md #6 */
void test_motion_is_published_immediately(void) {
  const uint32_t before_ms = node_hal_time_ms();
  start_models_task();

  device_reports(NODE_DEV_SIG_MOTION, MOTION_SENSOR, false,
                 (node_dev_value_t){.motion = true});

  /* Published while handling the event, with no time passing. */
  TEST_ASSERT_EQUAL_UINT32(before_ms, node_hal_time_ms());
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_publish_count());
  assert_last_published_status(g_motion_status, sizeof(g_motion_status));
}

/* AC: specs/11-mesh-model-mapping.md #6 */
void test_led_change_is_published_whatever_switched_it(void) {
  start_models_task();

  /* No OnOffSet arrived: the change came from elsewhere, such as its
   * default state at boot. */
  device_reports(NODE_DEV_SIG_ONOFF_CHANGED, LED, false,
                 (node_dev_value_t){.on = true});

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_publish_count());
  assert_last_published_status(g_led_on_status, sizeof(g_led_on_status));
}

/* AC: specs/11-mesh-model-mapping.md #6 */
/* AC: specs/11-mesh-model-mapping.md #7 */
void test_device_entering_fault_is_published_with_the_fault_flag(void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);

  device_reports(NODE_DEV_SIG_FAULT, SENSOR, true, (node_dev_value_t){0});

  TEST_ASSERT_EQUAL_size_t(2, node_mesh_models_port_mock_publish_count());
  assert_last_published_status(g_sensor_fault_status,
                               sizeof(g_sensor_fault_status));
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_fault_count());
  TEST_ASSERT_TRUE(node_mesh_log_mock_last_fault());
}

/* AC: specs/11-mesh-model-mapping.md #7 */
void test_device_leaving_fault_is_published_with_no_values_until_its_next_reading(
    void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);
  device_reports(NODE_DEV_SIG_FAULT, SENSOR, true, (node_dev_value_t){0});

  device_reports(NODE_DEV_SIG_FAULT, SENSOR, false, (node_dev_value_t){0});

  /* Device 0 with no fault and no values: an empty payload, not the reading
   * from before the fault. */
  assert_last_published_status(NULL, 0);
  TEST_ASSERT_FALSE(node_mesh_log_mock_last_fault());

  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);

  assert_last_published_status(g_reading_status, sizeof(g_reading_status));
}

/* AC: specs/11-mesh-model-mapping.md #6 */
void test_nothing_is_published_or_logged_when_no_publish_address_is_set(void) {
  start_models_task();
  node_mesh_models_port_mock_set_publish_result(NODE_MESH_ERR_STATE);

  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);
  device_reports(NODE_DEV_SIG_MOTION, MOTION_SENSOR, false,
                 (node_dev_value_t){.motion = true});

  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_publish_count());
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_log_mock_publish_failed_count());
}

void test_publish_the_mesh_refuses_is_logged(void) {
  start_models_task();
  node_mesh_models_port_mock_set_publish_result(NODE_MESH_ERR_MESH);

  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_publish_failed_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_MESH,
                        node_mesh_log_mock_last_publish_err());
}

void test_event_for_a_device_the_config_does_not_have_is_ignored_and_logged(
    void) {
  start_models_task();

  device_reports(NODE_DEV_SIG_MOTION, MISSING_DEVICE, false,
                 (node_dev_value_t){.motion = true});

  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_publish_count());
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_unknown_device_count());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_models_task_registers_the_vendor_model_when_it_starts);
  RUN_TEST(test_models_task_cannot_be_started_twice);
  RUN_TEST(test_config_with_more_devices_than_allowed_is_rejected_at_start);
  RUN_TEST(
      test_start_failure_is_reported_when_the_vendor_model_cannot_be_registered);
  RUN_TEST(
      test_sensor_reading_is_published_when_the_gateway_has_set_a_publish_address);
  RUN_TEST(test_motion_is_published_immediately);
  RUN_TEST(test_led_change_is_published_whatever_switched_it);
  RUN_TEST(test_device_entering_fault_is_published_with_the_fault_flag);
  RUN_TEST(
      test_device_leaving_fault_is_published_with_no_values_until_its_next_reading);
  RUN_TEST(test_nothing_is_published_or_logged_when_no_publish_address_is_set);
  RUN_TEST(test_publish_the_mesh_refuses_is_logged);
  RUN_TEST(
      test_event_for_a_device_the_config_does_not_have_is_ignored_and_logged);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void start_models_task(void) {
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_models_start(&g_config));
  run_until_idle();
}

static void device_reports(node_ao_sig_t sig, int dev_index, bool fault,
                           node_dev_value_t value) {
  const node_dev_evt_t evt = {
      .super = {.sig = sig},
      .dev_index = (uint8_t)dev_index,
      .fault = fault,
      .value = value,
      .ts_ms = node_hal_time_ms(),
  };
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));
  run_until_idle();
}

static void assert_last_published_status(const uint8_t *expected, size_t len) {
  const node_mesh_models_port_mock_msg_t msg =
      node_mesh_models_port_mock_last_publish();
  TEST_ASSERT_EQUAL_UINT8(NODE_MESH_MSG_OP_DEVICE_STATUS, msg.op);
  TEST_ASSERT_EQUAL_size_t(len, msg.len);
  if (len > 0u) {
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, msg.payload, len);
  }
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}
