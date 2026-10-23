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

/** @brief Opcode number the Node Server does not handle; the first one
 *         reserved for later specs. */
#define UNKNOWN_OP 0x07

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

/** @brief A later sensor reading of 20.00 °C and 0.00 %. */
static const node_dev_value_t g_later_reading = {.temp_c = 20.0f,
                                                 .rh_pct = 0.0f};

/** @brief DeviceStatus of the sensor (device 0) with g_later_reading: 200
 *         and 0 tenths. */
static const uint8_t g_later_reading_status[] = {0x10, 0x90, 0x03, 0x18, 0x00};

/** @brief DeviceStatus of the motion sensor (device 1) with no value yet. */
static const uint8_t g_motion_unknown_status[] = {0x08, 0x01};

/** @brief DeviceStatus of the LED (device 2) in FAULT. */
static const uint8_t g_led_fault_status[] = {0x08, 0x02, 0x30, 0x01};

/** @brief DeviceList of g_config: shtc3 "env1", pir "pir1", led "led1"
 *         (proto/testdata/node_vectors.json, three_devices). */
static const uint8_t g_device_list[] = {
    0x0a, 0x08, 0x08, 0x01, 0x12, 0x04, 'e', 'n', 'v', '1',
    0x0a, 0x08, 0x08, 0x02, 0x12, 0x04, 'p', 'i', 'r', '1',
    0x0a, 0x08, 0x08, 0x03, 0x12, 0x04, 'l', 'e', 'd', '1'};

/** @brief DeviceGet for the sensor, device 0. */
static const uint8_t g_get_sensor[] = {0x08, SENSOR};

/** @brief OnOffSet switching the LED (device 2) on. */
static const uint8_t g_switch_led_on[] = {0x08, LED, 0x10, 0x01};

/** @brief OnOffSet switching the LED (device 2) off. */
static const uint8_t g_switch_led_off[] = {0x08, LED};

/** @brief Where the gateway's requests come from in these tests. */
static const node_mesh_models_port_ctx_t g_gateway = {
    .net_idx = 0x000, .app_idx = 0x000, .addr = 0x0001};

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
 * @brief Send a request from g_gateway to the models AO and run the AOs
 *        until idle. Fails the test if the request is not queued.
 *
 * @param op      Opcode number of the request.
 * @param payload Request payload; may be NULL if @p len is 0.
 * @param len     Payload length in bytes.
 */
static void gateway_sends(uint8_t op, const uint8_t *payload, size_t len);

/**
 * @brief Check one reply sent to g_gateway.
 *
 * @param index    Position of the reply, from 0.
 * @param op       Expected opcode number.
 * @param expected Expected payload; may be NULL if @p len is 0.
 * @param len      Its length in bytes.
 */
static void assert_reply(size_t index, uint8_t op, const uint8_t *expected,
                         size_t len);

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

/* ---- Answering requests ---- */

/* AC: specs/11-mesh-model-mapping.md #4 */
void test_device_list_request_is_answered_with_the_configured_devices(void) {
  start_models_task();

  gateway_sends(NODE_MESH_MSG_OP_DEVICE_LIST_GET, NULL, 0);

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_LIST, g_device_list,
               sizeof(g_device_list));
}

/* AC: specs/11-mesh-model-mapping.md #4 */
void test_device_list_request_in_safe_mode_is_answered_with_an_empty_list(
    void) {
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_models_start(NULL));
  run_until_idle();

  gateway_sends(NODE_MESH_MSG_OP_DEVICE_LIST_GET, NULL, 0);

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_LIST, NULL, 0);
}

/* AC: specs/11-mesh-model-mapping.md #4 */
void test_device_request_without_a_device_is_answered_once_per_device_in_device_order(
    void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);
  device_reports(NODE_DEV_SIG_ONOFF_CHANGED, LED, false,
                 (node_dev_value_t){.on = true});

  /* An empty DeviceGet asks for every device. */
  gateway_sends(NODE_MESH_MSG_OP_DEVICE_GET, NULL, 0);

  TEST_ASSERT_EQUAL_size_t(3, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_STATUS, g_reading_status,
               sizeof(g_reading_status));
  assert_reply(1, NODE_MESH_MSG_OP_DEVICE_STATUS, g_motion_unknown_status,
               sizeof(g_motion_unknown_status));
  assert_reply(2, NODE_MESH_MSG_OP_DEVICE_STATUS, g_led_on_status,
               sizeof(g_led_on_status));
}

/* AC: specs/11-mesh-model-mapping.md #4 */
void test_device_request_is_answered_with_the_latest_published_value(void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_reading);
  device_reports(NODE_DEV_SIG_READING, SENSOR, false, g_later_reading);

  gateway_sends(NODE_MESH_MSG_OP_DEVICE_GET, g_get_sensor,
                sizeof(g_get_sensor));

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_STATUS, g_later_reading_status,
               sizeof(g_later_reading_status));
}

/* AC: specs/11-mesh-model-mapping.md #4 */
void test_device_request_for_a_device_the_config_does_not_have_gets_no_answer(
    void) {
  const uint8_t get_missing_device[] = {0x08, MISSING_DEVICE};
  start_models_task();

  gateway_sends(NODE_MESH_MSG_OP_DEVICE_GET, get_missing_device,
                sizeof(get_missing_device));

  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_MODELS_DROP_NO_DEVICE,
                        node_mesh_log_mock_last_drop_reason());
}

void test_malformed_request_gets_no_answer_and_is_logged(void) {
  /* A device field tag with no value after it. */
  const uint8_t cut_short[] = {0x08};
  start_models_task();

  gateway_sends(NODE_MESH_MSG_OP_DEVICE_GET, cut_short, sizeof(cut_short));

  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_request_dropped_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_MODELS_DROP_MALFORMED,
                        node_mesh_log_mock_last_drop_reason());
}

void test_request_with_an_unknown_opcode_gets_no_answer_and_is_logged(void) {
  start_models_task();

  gateway_sends(UNKNOWN_OP, NULL, 0);

  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_MODELS_DROP_UNKNOWN_OP,
                        node_mesh_log_mock_last_drop_reason());
}

/* AC: specs/11-mesh-model-mapping.md #4 */
void test_answer_goes_back_to_the_sender_with_the_keys_it_used(void) {
  const node_mesh_models_port_ctx_t other_sender = {
      .net_idx = 0x001, .app_idx = 0x002, .addr = 0x0042};
  start_models_task();

  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_OK,
      node_mesh_models_post_request(&other_sender,
                                    NODE_MESH_MSG_OP_DEVICE_LIST_GET, NULL, 0));
  run_until_idle();

  const node_mesh_models_port_mock_msg_t reply =
      node_mesh_models_port_mock_sent(0);
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  TEST_ASSERT_EQUAL_HEX16(other_sender.net_idx, reply.ctx.net_idx);
  TEST_ASSERT_EQUAL_HEX16(other_sender.app_idx, reply.ctx.app_idx);
  TEST_ASSERT_EQUAL_HEX16(other_sender.addr, reply.ctx.addr);
}

void test_answer_the_mesh_refuses_to_send_is_logged(void) {
  start_models_task();
  node_mesh_models_port_mock_set_send_result(NODE_MESH_ERR_MESH);

  gateway_sends(NODE_MESH_MSG_OP_DEVICE_LIST_GET, NULL, 0);

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_send_failed_count());
}

/* ---- Switching the LED ---- */

/* AC: specs/11-mesh-model-mapping.md #5 */
void test_switching_the_led_on_passes_the_command_to_the_devices(void) {
  start_models_task();

  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET, g_switch_led_on,
                sizeof(g_switch_led_on));

  TEST_ASSERT_EQUAL_size_t(1, node_dev_mock_onoff_count());
  TEST_ASSERT_EQUAL_INT(LED, node_dev_mock_last_onoff_dev());
  TEST_ASSERT_TRUE(node_dev_mock_last_onoff_on());
}

/* AC: specs/11-mesh-model-mapping.md #5 */
void test_acknowledged_switch_is_answered_with_the_requested_state(void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_ONOFF_CHANGED, LED, false,
                 (node_dev_value_t){.on = false});

  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET, g_switch_led_on,
                sizeof(g_switch_led_on));

  /* The devices task has not reported the change yet; the answer already
   * carries the requested state, as v1 applies it at once. */
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_STATUS, g_led_on_status,
               sizeof(g_led_on_status));
}

/* AC: specs/11-mesh-model-mapping.md #5 */
void test_unacknowledged_switch_gets_no_answer(void) {
  start_models_task();

  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET_UNACK, g_switch_led_on,
                sizeof(g_switch_led_on));

  TEST_ASSERT_EQUAL_size_t(1, node_dev_mock_onoff_count());
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
}

/* AC: specs/11-mesh-model-mapping.md #5 */
void test_switching_the_led_is_never_published_by_the_models_task_itself(void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_ONOFF_CHANGED, LED, false,
                 (node_dev_value_t){.on = true});
  const size_t published_before = node_mesh_models_port_mock_publish_count();

  /* Switching to the current state: only the devices task publishes, and
   * only on a change, so nothing is published again. */
  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET, g_switch_led_on,
                sizeof(g_switch_led_on));

  TEST_ASSERT_EQUAL_size_t(published_before,
                           node_mesh_models_port_mock_publish_count());
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
}

/* AC: specs/11-mesh-model-mapping.md #5 */
void test_switching_a_motion_sensor_is_refused_without_an_answer(void) {
  const uint8_t switch_motion_sensor_on[] = {0x08, MOTION_SENSOR, 0x10, 0x01};
  start_models_task();

  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET, switch_motion_sensor_on,
                sizeof(switch_motion_sensor_on));

  TEST_ASSERT_EQUAL_size_t(0, node_dev_mock_onoff_count());
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_MODELS_DROP_NOT_ACTUATOR,
                        node_mesh_log_mock_last_drop_reason());
}

/* AC: specs/11-mesh-model-mapping.md #5 */
/* AC: specs/11-mesh-model-mapping.md #7 */
void test_switching_an_led_in_fault_is_answered_with_its_fault_and_no_state(
    void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_FAULT, LED, true, (node_dev_value_t){0});

  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET, g_switch_led_on,
                sizeof(g_switch_led_on));

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_STATUS, g_led_fault_status,
               sizeof(g_led_fault_status));
}

/* AC: specs/11-mesh-model-mapping.md #5 */
void test_switching_the_led_when_the_devices_queue_is_full_is_answered_with_the_known_state_and_logged(
    void) {
  start_models_task();
  device_reports(NODE_DEV_SIG_ONOFF_CHANGED, LED, false,
                 (node_dev_value_t){.on = true});
  node_dev_mock_set_onoff_result(NODE_DEV_ERR_FULL);

  gateway_sends(NODE_MESH_MSG_OP_ONOFF_SET, g_switch_led_off,
                sizeof(g_switch_led_off));

  /* The LED stays on: the answer says so, not the requested off. */
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_models_port_mock_send_count());
  assert_reply(0, NODE_MESH_MSG_OP_DEVICE_STATUS, g_led_on_status,
               sizeof(g_led_on_status));
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_onoff_not_queued_count());
}

/* ---- Delivering requests ---- */

void test_request_longer_than_8_bytes_is_dropped_before_it_reaches_the_models_task(
    void) {
  const uint8_t too_long[NODE_MESH_MSG_MAX_UNSEGMENTED + 1] = {0};
  start_models_task();

  node_mesh_err_t err = node_mesh_models_post_request(
      &g_gateway, NODE_MESH_MSG_OP_DEVICE_GET, too_long, sizeof(too_long));
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
  TEST_ASSERT_EQUAL_INT(NODE_MESH_MODELS_DROP_TOO_LONG,
                        node_mesh_log_mock_last_drop_reason());
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
}

void test_requests_sent_before_the_models_task_starts_are_reported_as_lost(
    void) {
  node_mesh_err_t err = node_mesh_models_post_request(
      &g_gateway, NODE_MESH_MSG_OP_DEVICE_LIST_GET, NULL, 0);
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_STATE, err);
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_post_failed_count());
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_models_port_mock_send_count());
}

void test_requests_are_reported_as_lost_when_the_models_task_queue_is_full(
    void) {
  start_models_task();
  size_t lost = 0;

  /* More requests than any queue the host port can hold, with none
   * dispatched in between. */
  for (size_t i = 0; i <= NODE_AO_PORT_MOCK_MAX_QUEUE_LEN; i++) {
    if (node_mesh_models_post_request(&g_gateway,
                                      NODE_MESH_MSG_OP_DEVICE_LIST_GET, NULL,
                                      0) != NODE_MESH_OK) {
      lost++;
    }
  }

  TEST_ASSERT_GREATER_THAN_size_t(0, lost);
  TEST_ASSERT_EQUAL_size_t(lost, node_mesh_log_mock_post_failed_count());
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
  RUN_TEST(test_device_list_request_is_answered_with_the_configured_devices);
  RUN_TEST(
      test_device_list_request_in_safe_mode_is_answered_with_an_empty_list);
  RUN_TEST(
      test_device_request_without_a_device_is_answered_once_per_device_in_device_order);
  RUN_TEST(test_device_request_is_answered_with_the_latest_published_value);
  RUN_TEST(
      test_device_request_for_a_device_the_config_does_not_have_gets_no_answer);
  RUN_TEST(test_malformed_request_gets_no_answer_and_is_logged);
  RUN_TEST(test_request_with_an_unknown_opcode_gets_no_answer_and_is_logged);
  RUN_TEST(test_answer_goes_back_to_the_sender_with_the_keys_it_used);
  RUN_TEST(test_answer_the_mesh_refuses_to_send_is_logged);
  RUN_TEST(test_switching_the_led_on_passes_the_command_to_the_devices);
  RUN_TEST(test_acknowledged_switch_is_answered_with_the_requested_state);
  RUN_TEST(test_unacknowledged_switch_gets_no_answer);
  RUN_TEST(test_switching_the_led_is_never_published_by_the_models_task_itself);
  RUN_TEST(test_switching_a_motion_sensor_is_refused_without_an_answer);
  RUN_TEST(
      test_switching_an_led_in_fault_is_answered_with_its_fault_and_no_state);
  RUN_TEST(
      test_switching_the_led_when_the_devices_queue_is_full_is_answered_with_the_known_state_and_logged);
  RUN_TEST(
      test_request_longer_than_8_bytes_is_dropped_before_it_reaches_the_models_task);
  RUN_TEST(
      test_requests_sent_before_the_models_task_starts_are_reported_as_lost);
  RUN_TEST(
      test_requests_are_reported_as_lost_when_the_models_task_queue_is_full);
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

static void gateway_sends(uint8_t op, const uint8_t *payload, size_t len) {
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_models_post_request(
                                          &g_gateway, op, payload, len));
  run_until_idle();
}

static void assert_reply(size_t index, uint8_t op, const uint8_t *expected,
                         size_t len) {
  const node_mesh_models_port_mock_msg_t reply =
      node_mesh_models_port_mock_sent(index);
  TEST_ASSERT_EQUAL_HEX16(g_gateway.addr, reply.ctx.addr);
  TEST_ASSERT_EQUAL_UINT8(op, reply.op);
  TEST_ASSERT_EQUAL_size_t(len, reply.len);
  if (len > 0u) {
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reply.payload, len);
  }
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
