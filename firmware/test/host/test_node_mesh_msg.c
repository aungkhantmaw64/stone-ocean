/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "node_config.h"
#include "node_dev.h"
#include "node_mesh_err.h"
#include "node_mesh_msg.h"
#include "node_vectors.h"
#include "stone_ocean/mesh/v1/node.pb.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Room for any payload in these tests: larger than the largest
 *         DeviceList. */
#define PAYLOAD_CAP 256

/** @brief A device index one past the last one allowed. */
#define FIRST_DEVICE_OUT_OF_RANGE NODE_CONFIG_MAX_DEVICES

/** @brief Size of a failure message built inside a loop over vectors. */
#define MESSAGE_LEN 96

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief proto/testdata/node_vectors.json, parsed in setUp(). */
static cJSON *g_vectors = NULL;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Find a vector by name. Fails the test if there is none.
 *
 * @param section Array in the vector file, such as "device_status".
 * @param name    The vector's name.
 * @return The vector.
 */
static const cJSON *vector(const char *section, const char *name);

/**
 * @brief Payload bytes of a vector, from its "hex". Fails the test on a bad
 *        hex string.
 *
 * @param v   The vector.
 * @param out Receives the bytes.
 * @param cap Size of @p out.
 * @return Number of bytes.
 */
static size_t vector_bytes(const cJSON *v, uint8_t *out, size_t cap);

/**
 * @brief Encode a device_status vector's input with the firmware encoder.
 *
 * @param v   The vector.
 * @param out Receives the payload.
 * @param cap Size of @p out.
 * @param len Receives the payload length.
 * @return The encoder's result.
 */
static node_mesh_err_t encode_status_vector(const cJSON *v, uint8_t *out,
                                            size_t cap, size_t *len);

/**
 * @brief Encode a device_list vector's devices with the firmware encoder.
 *
 * @param v   The vector.
 * @param out Receives the payload.
 * @param cap Size of @p out.
 * @param len Receives the payload length.
 * @return The encoder's result.
 */
static node_mesh_err_t encode_list_vector(const cJSON *v, uint8_t *out,
                                          size_t cap, size_t *len);

/**
 * @brief Check that the firmware encodes a device_status vector to its bytes.
 *
 * @param name The vector's name.
 */
static void assert_status_vector_encodes(const char *name);

/**
 * @brief Check that the firmware encodes a device_list vector to its bytes.
 *
 * @param name The vector's name.
 */
static void assert_list_vector_encodes(const char *name);

/**
 * @brief Check that the firmware decodes a device_get vector to its fields.
 *
 * @param v The vector.
 */
static void assert_device_get_vector_decodes(const cJSON *v);

/**
 * @brief Check that the firmware decodes an onoff_set vector to its fields.
 *
 * @param v The vector.
 */
static void assert_onoff_vector_decodes(const cJSON *v);

/**
 * @brief Config device type of a type name in the vector file.
 *
 * @param name "shtc3", "pir" or "led". Fails the test otherwise.
 * @return The type.
 */
static node_config_dev_type_t device_type_named(const char *name);

/**
 * @brief A number from the vector file; the string "nan" stands for a value
 *        that is not a number.
 *
 * @param item The JSON item, or NULL.
 * @return The value; 0 if @p item is NULL.
 */
static float number_or_nan(const cJSON *item);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  g_vectors = cJSON_ParseWithLength(g_node_vectors, sizeof(g_node_vectors));
  TEST_ASSERT_NOT_NULL_MESSAGE(g_vectors,
                               "proto/testdata/node_vectors.json must parse");
}

void tearDown(void) {
  cJSON_Delete(g_vectors);
  g_vectors = NULL;
}

/* ---- Sending sensor values ---- */

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_temperature_and_humidity_are_sent_in_tenths_of_a_degree_and_a_percent(
    void) {
  /* 23.45 °C and 45.67 % are sent as 235 and 457. */
  assert_status_vector_encodes("shtc3_typical");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_negative_temperature_is_sent_as_a_negative_number_of_tenths(void) {
  /* -12.34 °C is sent as -123. */
  assert_status_vector_encodes("shtc3_negative_temperature");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_temperature_and_humidity_halfway_between_tenths_are_rounded_away_from_zero(
    void) {
  /* -0.25 °C becomes -3 tenths and 50.25 % becomes 503. */
  assert_status_vector_encodes("shtc3_halfway_rounds_away_from_zero");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_humidity_of_0_and_100_percent_is_sent_as_0_and_1000(void) {
  assert_status_vector_encodes("shtc3_dry");
  assert_status_vector_encodes("shtc3_saturated");
}

/* AC: specs/11-mesh-model-mapping.md #3 */
void test_temperature_and_humidity_outside_the_sensor_range_are_sent_clamped_to_it(
    void) {
  /* 500 °C and -3 % become 130 °C and 0 %; -60 °C and 120 % become -45 °C
   * and 100 %. */
  assert_status_vector_encodes("shtc3_clamped_hot");
  assert_status_vector_encodes("shtc3_clamped_cold");
}

void test_temperature_that_is_not_a_number_is_left_out_of_the_status(void) {
  assert_status_vector_encodes("shtc3_temperature_not_a_number");
}

/* ---- Sending motion and LED state ---- */

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_both_motion_and_no_motion_are_sent_so_no_motion_is_never_read_as_unknown(
    void) {
  assert_status_vector_encodes("pir_motion");
  assert_status_vector_encodes("pir_no_motion");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_both_led_on_and_led_off_are_sent(void) {
  assert_status_vector_encodes("led_on");
  assert_status_vector_encodes("led_off");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_motion_sensor_status_carries_no_temperature_or_humidity(void) {
  assert_status_vector_encodes("pir_ignores_climate_values");
}

/* ---- Unknown values ---- */

/* AC: specs/11-mesh-model-mapping.md #7 */
void test_sensor_in_fault_is_sent_with_the_fault_flag_and_no_values(void) {
  assert_status_vector_encodes("shtc3_fault");
}

/* AC: specs/11-mesh-model-mapping.md #7 */
void test_sensor_with_no_value_yet_is_sent_with_no_values(void) {
  assert_status_vector_encodes("shtc3_no_value_yet");
}

/* AC: specs/11-mesh-model-mapping.md #7 */
void test_status_of_an_led_at_device_0_with_no_value_yet_is_a_valid_empty_message(
    void) {
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 1;

  node_mesh_err_t err = node_mesh_msg_encode_device_status(
      0, NODE_CONFIG_DEV_LED, NULL, false, payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_size_t(0, len);
}

/* ---- Fitting one mesh message ---- */

/* AC: specs/11-mesh-model-mapping.md #3 */
void test_largest_sensor_status_fits_one_unsegmented_mesh_message(void) {
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 0;

  node_mesh_err_t err =
      encode_status_vector(vector("device_status", "shtc3_largest_status"),
                           payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_size_t(NODE_MESH_MSG_MAX_UNSEGMENTED, len);
  assert_status_vector_encodes("shtc3_largest_status");
}

/* AC: specs/11-mesh-model-mapping.md #3 */
void test_every_request_the_gateway_can_send_fits_one_unsegmented_mesh_message(
    void) {
  /* nanopb's worst-case sizes cover every value a field can hold. */
  TEST_ASSERT_LESS_OR_EQUAL_size_t(NODE_MESH_MSG_MAX_UNSEGMENTED,
                                   stoneocean_mesh_v1_DeviceListGet_size);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(NODE_MESH_MSG_MAX_UNSEGMENTED,
                                   stoneocean_mesh_v1_DeviceGet_size);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(NODE_MESH_MSG_MAX_UNSEGMENTED,
                                   stoneocean_mesh_v1_OnOffSet_size);
}

/* ---- The device list ---- */

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_device_list_names_every_device_with_its_type_and_id_in_config_order(
    void) {
  assert_list_vector_encodes("three_devices");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_device_list_of_eight_devices_with_the_longest_ids_fits_the_list_buffer(
    void) {
  uint8_t payload[NODE_MESH_MSG_DEVICE_LIST_MAX_LEN] = {0};
  size_t len = 0;

  node_mesh_err_t err =
      encode_list_vector(vector("device_list", "eight_longest_ids"), payload,
                         sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_size_t(NODE_MESH_MSG_DEVICE_LIST_MAX_LEN, len);
  assert_list_vector_encodes("eight_longest_ids");
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_device_list_in_safe_mode_is_empty(void) {
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 1;

  node_mesh_err_t err =
      node_mesh_msg_encode_device_list(NULL, payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_size_t(0, len);
  assert_list_vector_encodes("safe_mode");
}

/* ---- Reading requests ---- */

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_device_request_without_a_device_asks_for_every_device(void) {
  assert_device_get_vector_decodes(vector("device_get", "every_device"));
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_device_request_for_device_0_asks_for_device_0_only(void) {
  bool all = true;
  int dev_index = -1;
  const uint8_t device_0[] = {0x08, 0x00};

  node_mesh_err_t err = node_mesh_msg_decode_device_get(
      device_0, sizeof(device_0), &all, &dev_index);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_FALSE(all);
  TEST_ASSERT_EQUAL_INT(0, dev_index);
  assert_device_get_vector_decodes(vector("device_get", "device_0"));
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_switch_request_is_read_as_a_device_index_and_an_on_off_state(void) {
  assert_onoff_vector_decodes(vector("onoff_set", "led_2_on"));
  /* An empty payload is device 0, off: every field at its default. */
  assert_onoff_vector_decodes(vector("onoff_set", "device_0_off"));
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_request_fields_unknown_to_this_firmware_are_ignored(void) {
  assert_device_get_vector_decodes(vector("device_get", "unknown_field"));
}

/* ---- Rejecting bad input ---- */

void test_request_longer_than_8_bytes_is_rejected(void) {
  /* Nine zero bytes: field 0 is invalid anyway, but the length check comes
   * first. */
  const uint8_t too_long[NODE_MESH_MSG_MAX_UNSEGMENTED + 1] = {0};
  bool all = false;
  int dev_index = 0;
  bool on = false;

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_device_get(
                            too_long, sizeof(too_long), &all, &dev_index));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_onoff_set(
                            too_long, sizeof(too_long), &dev_index, &on));
}

void test_request_that_is_cut_short_is_rejected(void) {
  /* A device field tag with no value after it. */
  const uint8_t cut_short[] = {0x08};
  bool all = true;
  int dev_index = 5;
  bool on = true;

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_device_get(
                            cut_short, sizeof(cut_short), &all, &dev_index));
  TEST_ASSERT_FALSE(all);
  TEST_ASSERT_EQUAL_INT(0, dev_index);
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_onoff_set(
                            cut_short, sizeof(cut_short), &dev_index, &on));
  TEST_ASSERT_FALSE(on);
}

void test_request_for_device_8_or_above_is_rejected(void) {
  const uint8_t device_8[] = {0x08, FIRST_DEVICE_OUT_OF_RANGE};
  /* Device 300 needs a two-byte varint. */
  const uint8_t device_300[] = {0x08, 0xac, 0x02, 0x10, 0x01};
  bool all = false;
  int dev_index = 0;
  bool on = false;

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_device_get(
                            device_8, sizeof(device_8), &all, &dev_index));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_onoff_set(
                            device_300, sizeof(device_300), &dev_index, &on));
}

void test_status_for_a_device_index_or_type_out_of_range_is_not_encoded(void) {
  const node_dev_value_t on = {.on = true};
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 1;

  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_ERR_ARG,
      node_mesh_msg_encode_device_status(-1, NODE_CONFIG_DEV_LED, &on, false,
                                         payload, sizeof(payload), &len));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_encode_device_status(
                            FIRST_DEVICE_OUT_OF_RANGE, NODE_CONFIG_DEV_LED, &on,
                            false, payload, sizeof(payload), &len));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_encode_device_status(
                            0, (node_config_dev_type_t)99, &on, false, payload,
                            sizeof(payload), &len));
  TEST_ASSERT_EQUAL_size_t(0, len);
}

void test_status_that_does_not_fit_the_buffer_is_not_encoded(void) {
  const node_dev_value_t reading = {.temp_c = 23.45f, .rh_pct = 45.67f};
  /* The status needs 6 bytes. */
  uint8_t payload[5] = {0};
  size_t len = 1;

  node_mesh_err_t err =
      node_mesh_msg_encode_device_status(0, NODE_CONFIG_DEV_SHTC3, &reading,
                                         false, payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, len);
}

void test_device_list_of_a_config_with_more_devices_than_allowed_is_not_encoded(
    void) {
  node_config_t config = {0};
  config.n_dev = NODE_CONFIG_MAX_DEVICES + 1;
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 1;

  node_mesh_err_t err =
      node_mesh_msg_encode_device_list(&config, payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, len);
}

void test_encoding_or_decoding_without_somewhere_to_put_the_result_is_rejected(
    void) {
  const node_dev_value_t on = {.on = true};
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 0;
  bool all = false;
  int dev_index = 0;
  bool state = false;

  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_ERR_ARG,
      node_mesh_msg_encode_device_status(0, NODE_CONFIG_DEV_LED, &on, false,
                                         NULL, sizeof(payload), &len));
  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_ERR_ARG,
      node_mesh_msg_encode_device_status(0, NODE_CONFIG_DEV_LED, &on, false,
                                         payload, sizeof(payload), NULL));
  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_ERR_ARG,
      node_mesh_msg_encode_device_list(NULL, NULL, sizeof(payload), &len));
  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_ERR_ARG,
      node_mesh_msg_encode_device_list(NULL, payload, sizeof(payload), NULL));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, node_mesh_msg_decode_device_get(
                                               NULL, 0, NULL, &dev_index));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_device_get(NULL, 0, &all, NULL));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG,
                        node_mesh_msg_decode_onoff_set(NULL, 0, NULL, &state));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, node_mesh_msg_decode_onoff_set(
                                               NULL, 0, &dev_index, NULL));
  /* A missing payload is only allowed when it is empty. */
  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, node_mesh_msg_decode_device_get(
                                               NULL, 2, &all, &dev_index));
}

/* ---- Shared vectors ---- */

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_every_shared_status_and_device_list_vector_encodes_to_its_bytes(
    void) {
  const cJSON *v = NULL;
  cJSON_ArrayForEach(
      v, cJSON_GetObjectItemCaseSensitive(g_vectors, "device_status")) {
    const char *name =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "name"));
    assert_status_vector_encodes(name);
    uint8_t expected[PAYLOAD_CAP] = {0};
    char message[MESSAGE_LEN] = {0};
    (void)snprintf(message, sizeof(message), "status vector %s", name);
    TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(
        NODE_MESH_MSG_MAX_UNSEGMENTED,
        vector_bytes(v, expected, sizeof(expected)), message);
  }
  cJSON_ArrayForEach(
      v, cJSON_GetObjectItemCaseSensitive(g_vectors, "device_list")) {
    assert_list_vector_encodes(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "name")));
  }
}

/* AC: specs/11-mesh-model-mapping.md #2 */
void test_every_shared_request_vector_decodes_to_its_fields(void) {
  const cJSON *v = NULL;
  cJSON_ArrayForEach(
      v, cJSON_GetObjectItemCaseSensitive(g_vectors, "device_get")) {
    assert_device_get_vector_decodes(v);
  }
  cJSON_ArrayForEach(v,
                     cJSON_GetObjectItemCaseSensitive(g_vectors, "onoff_set")) {
    assert_onoff_vector_decodes(v);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_temperature_and_humidity_are_sent_in_tenths_of_a_degree_and_a_percent);
  RUN_TEST(test_negative_temperature_is_sent_as_a_negative_number_of_tenths);
  RUN_TEST(
      test_temperature_and_humidity_halfway_between_tenths_are_rounded_away_from_zero);
  RUN_TEST(test_humidity_of_0_and_100_percent_is_sent_as_0_and_1000);
  RUN_TEST(
      test_temperature_and_humidity_outside_the_sensor_range_are_sent_clamped_to_it);
  RUN_TEST(test_temperature_that_is_not_a_number_is_left_out_of_the_status);
  RUN_TEST(
      test_both_motion_and_no_motion_are_sent_so_no_motion_is_never_read_as_unknown);
  RUN_TEST(test_both_led_on_and_led_off_are_sent);
  RUN_TEST(test_motion_sensor_status_carries_no_temperature_or_humidity);
  RUN_TEST(test_sensor_in_fault_is_sent_with_the_fault_flag_and_no_values);
  RUN_TEST(test_sensor_with_no_value_yet_is_sent_with_no_values);
  RUN_TEST(
      test_status_of_an_led_at_device_0_with_no_value_yet_is_a_valid_empty_message);
  RUN_TEST(test_largest_sensor_status_fits_one_unsegmented_mesh_message);
  RUN_TEST(
      test_every_request_the_gateway_can_send_fits_one_unsegmented_mesh_message);
  RUN_TEST(
      test_device_list_names_every_device_with_its_type_and_id_in_config_order);
  RUN_TEST(
      test_device_list_of_eight_devices_with_the_longest_ids_fits_the_list_buffer);
  RUN_TEST(test_device_list_in_safe_mode_is_empty);
  RUN_TEST(test_device_request_without_a_device_asks_for_every_device);
  RUN_TEST(test_device_request_for_device_0_asks_for_device_0_only);
  RUN_TEST(test_switch_request_is_read_as_a_device_index_and_an_on_off_state);
  RUN_TEST(test_request_fields_unknown_to_this_firmware_are_ignored);
  RUN_TEST(test_request_longer_than_8_bytes_is_rejected);
  RUN_TEST(test_request_that_is_cut_short_is_rejected);
  RUN_TEST(test_request_for_device_8_or_above_is_rejected);
  RUN_TEST(test_status_for_a_device_index_or_type_out_of_range_is_not_encoded);
  RUN_TEST(test_status_that_does_not_fit_the_buffer_is_not_encoded);
  RUN_TEST(
      test_device_list_of_a_config_with_more_devices_than_allowed_is_not_encoded);
  RUN_TEST(
      test_encoding_or_decoding_without_somewhere_to_put_the_result_is_rejected);
  RUN_TEST(
      test_every_shared_status_and_device_list_vector_encodes_to_its_bytes);
  RUN_TEST(test_every_shared_request_vector_decodes_to_its_fields);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static const cJSON *vector(const char *section, const char *name) {
  const cJSON *v = NULL;
  cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(g_vectors, section)) {
    const char *v_name =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "name"));
    if ((v_name != NULL) && (strcmp(v_name, name) == 0)) {
      return v;
    }
  }
  TEST_FAIL_MESSAGE("vector not found in proto/testdata/node_vectors.json");
  return NULL;
}

static size_t vector_bytes(const cJSON *v, uint8_t *out, size_t cap) {
  const char *hex =
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "hex"));
  TEST_ASSERT_NOT_NULL_MESSAGE(hex, "vector has no hex");
  const size_t hex_len = strlen(hex);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, hex_len % 2u, "odd-length hex");
  TEST_ASSERT_LESS_OR_EQUAL_size_t(cap, hex_len / 2u);
  for (size_t i = 0; i < (hex_len / 2u); i++) {
    unsigned byte = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sscanf(&hex[2u * i], "%2x", &byte),
                                  "bad hex digit");
    out[i] = (uint8_t)byte;
  }
  return hex_len / 2u;
}

static node_mesh_err_t encode_status_vector(const cJSON *v, uint8_t *out,
                                            size_t cap, size_t *len) {
  const cJSON *input = cJSON_GetObjectItemCaseSensitive(v, "input");
  const cJSON *value_json = cJSON_GetObjectItemCaseSensitive(input, "value");
  node_dev_value_t value = {0};
  if (value_json != NULL) {
    value.temp_c =
        number_or_nan(cJSON_GetObjectItemCaseSensitive(value_json, "temp_c"));
    value.rh_pct =
        number_or_nan(cJSON_GetObjectItemCaseSensitive(value_json, "rh_pct"));
    value.motion =
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(value_json, "motion"));
    value.on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(value_json, "on"));
  }
  const int dev_index =
      cJSON_GetObjectItemCaseSensitive(input, "device")->valueint;
  const node_config_dev_type_t type = device_type_named(
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(input, "type")));
  const bool fault =
      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(input, "fault"));
  return node_mesh_msg_encode_device_status(
      dev_index, type, (value_json != NULL) ? &value : NULL, fault, out, cap,
      len);
}

static node_mesh_err_t encode_list_vector(const cJSON *v, uint8_t *out,
                                          size_t cap, size_t *len) {
  const cJSON *devices = cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(v, "input"), "devices");
  if (cJSON_GetArraySize(devices) == 0) {
    return node_mesh_msg_encode_device_list(NULL, out, cap, len);
  }
  node_config_t config = {0};
  const cJSON *device = NULL;
  cJSON_ArrayForEach(device, devices) {
    TEST_ASSERT_LESS_THAN_INT(NODE_CONFIG_MAX_DEVICES, config.n_dev);
    node_config_dev_t *dev = &config.dev[config.n_dev];
    dev->type = device_type_named(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(device, "type")));
    const char *id =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(device, "id"));
    TEST_ASSERT_LESS_THAN_size_t(sizeof(dev->id), strlen(id));
    (void)snprintf(dev->id, sizeof(dev->id), "%s", id);
    config.n_dev++;
  }
  return node_mesh_msg_encode_device_list(&config, out, cap, len);
}

static void assert_status_vector_encodes(const char *name) {
  const cJSON *v = vector("device_status", name);
  uint8_t expected[PAYLOAD_CAP] = {0};
  const size_t expected_len = vector_bytes(v, expected, sizeof(expected));
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 0;
  char message[MESSAGE_LEN] = {0};
  (void)snprintf(message, sizeof(message), "status vector %s", name);

  node_mesh_err_t err = encode_status_vector(v, payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_MESH_OK, err, message);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(expected_len, len, message);
  if (expected_len > 0u) {
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected, payload, expected_len,
                                         message);
  }
}

static void assert_list_vector_encodes(const char *name) {
  const cJSON *v = vector("device_list", name);
  uint8_t expected[PAYLOAD_CAP] = {0};
  const size_t expected_len = vector_bytes(v, expected, sizeof(expected));
  uint8_t payload[PAYLOAD_CAP] = {0};
  size_t len = 0;
  char message[MESSAGE_LEN] = {0};
  (void)snprintf(message, sizeof(message), "device list vector %s", name);

  node_mesh_err_t err = encode_list_vector(v, payload, sizeof(payload), &len);

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_MESH_OK, err, message);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(expected_len, len, message);
  if (expected_len > 0u) {
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected, payload, expected_len,
                                         message);
  }
}

static void assert_device_get_vector_decodes(const cJSON *v) {
  uint8_t payload[PAYLOAD_CAP] = {0};
  const size_t len = vector_bytes(v, payload, sizeof(payload));
  const cJSON *device = cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(v, "fields"), "device");
  bool all = false;
  int dev_index = -1;
  char message[MESSAGE_LEN] = {0};
  (void)snprintf(
      message, sizeof(message), "device_get vector %s",
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "name")));

  node_mesh_err_t err =
      node_mesh_msg_decode_device_get(payload, len, &all, &dev_index);

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_MESH_OK, err, message);
  TEST_ASSERT_EQUAL_MESSAGE(device == NULL, all, message);
  TEST_ASSERT_EQUAL_INT_MESSAGE((device != NULL) ? device->valueint : 0,
                                dev_index, message);
}

static void assert_onoff_vector_decodes(const cJSON *v) {
  uint8_t payload[PAYLOAD_CAP] = {0};
  const size_t len = vector_bytes(v, payload, sizeof(payload));
  const cJSON *fields = cJSON_GetObjectItemCaseSensitive(v, "fields");
  int dev_index = -1;
  bool on = false;
  char message[MESSAGE_LEN] = {0};
  (void)snprintf(
      message, sizeof(message), "onoff_set vector %s",
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(v, "name")));

  node_mesh_err_t err =
      node_mesh_msg_decode_onoff_set(payload, len, &dev_index, &on);

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_MESH_OK, err, message);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      cJSON_GetObjectItemCaseSensitive(fields, "device")->valueint, dev_index,
      message);
  TEST_ASSERT_EQUAL_MESSAGE(
      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(fields, "on")), on,
      message);
}

static node_config_dev_type_t device_type_named(const char *name) {
  TEST_ASSERT_NOT_NULL(name);
  if (strcmp(name, "shtc3") == 0) {
    return NODE_CONFIG_DEV_SHTC3;
  }
  if (strcmp(name, "pir") == 0) {
    return NODE_CONFIG_DEV_PIR;
  }
  if (strcmp(name, "led") == 0) {
    return NODE_CONFIG_DEV_LED;
  }
  TEST_FAIL_MESSAGE("unknown device type in the vector file");
  return NODE_CONFIG_DEV_LED;
}

static float number_or_nan(const cJSON *item) {
  if (item == NULL) {
    return 0.0f;
  }
  if (cJSON_IsString(item) && (strcmp(item->valuestring, "nan") == 0)) {
    return NAN;
  }
  return (float)item->valuedouble;
}
