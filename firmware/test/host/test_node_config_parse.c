/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <string.h>

#include "node_config.h"
#include "node_config_example.h"
#include "node_config_log_mock.h"
#include "node_config_test_configs.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Debounce time the parser uses when "debounce_ms" is omitted. */
#define DEFAULT_DEBOUNCE_MS 50

/** @brief Shortest report interval used when omitted, in ms. */
#define DEFAULT_MIN_INTERVAL_MS 5000

/** @brief Longest report interval used when omitted, in ms. */
#define DEFAULT_MAX_INTERVAL_MS 60000

/** @brief Temperature change that triggers a report when omitted, in °C. */
#define DEFAULT_DELTA_TEMP_C 0.5f

/** @brief Humidity change that triggers a report when omitted, in percent
 *         relative humidity. */
#define DEFAULT_DELTA_RH_PCT 2.0f

/** @brief Index of the temperature sensor in the example config. */
#define EXAMPLE_SENSOR 0

/** @brief Index of the motion sensor in the example config. */
#define EXAMPLE_MOTION_SENSOR 1

/** @brief Index of the LED in the example config. */
#define EXAMPLE_LED 2

/** @brief A node name of 31 characters, the longest that fits. */
#define NAME_OF_31_CHARACTERS "abcdefghijklmnopqrstuvwxyz01234"

/** @brief A node name of 32 characters, one too many. */
#define NAME_OF_32_CHARACTERS NAME_OF_31_CHARACTERS "5"

/** @brief A device id of 15 characters, the longest that fits. */
#define ID_OF_15_CHARACTERS "abcdefghijklmno"

/** @brief A device id of 16 characters, one too many. */
#define ID_OF_16_CHARACTERS ID_OF_15_CHARACTERS "p"

/** @brief A config with the given top-level members after "version". */
#define CONFIG_WITH_VERSION(version, rest) "{\"version\":" #version "," rest "}"

/** @brief I2C bus "i2c0" on port 0 at the given clock, in Hz. */
#define BUS_AT_HZ(freq_hz) \
  "{\"id\":\"i2c0\",\"port\":0,\"sda\":21,\"scl\":22,\"freq_hz\":" #freq_hz "}"

/** @brief I2C bus "i2c0" on the given port. */
#define BUS_ON_PORT(port)            \
  "{\"id\":\"i2c0\",\"port\":" #port \
  ",\"sda\":21,\"scl\":22,\"freq_hz\":100000}"

/** @brief SHTC3 on bus "i2c0" sampling at the given interval, in ms. */
#define SHTC3_SAMPLING_EVERY(interval_ms)                 \
  "{\"id\":\"env1\",\"type\":\"shtc3\",\"bus\":\"i2c0\"," \
  "\"sample_interval_ms\":" #interval_ms "}"

/** @brief LED with GPIO 25 and the given extra members. */
#define LED_WITH(members)                                          \
  "{\"id\":\"led1\",\"type\":\"led\",\"gpio\":25,\"active_high\":" \
  "true," members "}"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Config filled by the last parse_text() or parse_example() call. */
static node_config_t g_config = {0};

/** @brief Error details of the last parse_text() or parse_example() call. */
static node_config_err_t g_err = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

static node_config_err_code_t parse_text(const char *json);
static node_config_err_code_t parse_example(void);
static node_config_err_code_t parse_padded_to(size_t len);
static void assert_rejected(node_config_err_code_t code,
                            node_config_err_code_t expected_code,
                            const char *expected_path);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_config_log_mock_reset();
  (void)memset(&g_config, 0, sizeof(g_config));
  (void)memset(&g_err, 0, sizeof(g_err));
}

void tearDown(void) {}

/* ---- Accepting valid configs ---- */

/* AC: specs/03-config-parser.md #1, #2 */
void test_example_config_is_accepted_without_warnings(void) {
  node_config_err_code_t code = parse_example();

  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, code);
  TEST_ASSERT_EQUAL_size_t(0, node_config_log_mock_count());
}

/* AC: specs/03-config-parser.md #2 */
void test_example_config_fills_in_the_node_name_and_i2c_bus_settings(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_example());

  TEST_ASSERT_EQUAL_INT(1, g_config.version);
  TEST_ASSERT_EQUAL_STRING("lab-node-1", g_config.node_name);
  TEST_ASSERT_EQUAL_INT(1, g_config.n_i2c);
  TEST_ASSERT_EQUAL_STRING("i2c0", g_config.i2c[0].id);
  TEST_ASSERT_EQUAL_INT(0, g_config.i2c[0].port);
  TEST_ASSERT_EQUAL_INT(21, g_config.i2c[0].sda);
  TEST_ASSERT_EQUAL_INT(22, g_config.i2c[0].scl);
  TEST_ASSERT_EQUAL_UINT32(100000, g_config.i2c[0].freq_hz);
}

/* AC: specs/03-config-parser.md #2 */
void test_example_config_fills_in_every_temperature_sensor_setting(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_example());
  const node_config_dev_t *sensor = &g_config.dev[EXAMPLE_SENSOR];

  TEST_ASSERT_EQUAL_INT(3, g_config.n_dev);
  TEST_ASSERT_EQUAL_STRING("env1", sensor->id);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_DEV_SHTC3, sensor->type);
  TEST_ASSERT_EQUAL_INT(0, sensor->shtc3.bus_index);
  TEST_ASSERT_EQUAL_UINT32(10000, sensor->shtc3.sample_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(5000, sensor->shtc3.report.min_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(60000, sensor->shtc3.report.max_interval_ms);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, sensor->shtc3.report.delta_temp_c);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, sensor->shtc3.report.delta_rh_pct);
}

/* AC: specs/03-config-parser.md #2 */
void test_example_config_fills_in_every_motion_sensor_setting(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_example());
  const node_config_dev_t *motion = &g_config.dev[EXAMPLE_MOTION_SENSOR];

  TEST_ASSERT_EQUAL_STRING("pir1", motion->id);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_DEV_PIR, motion->type);
  TEST_ASSERT_EQUAL_INT(27, motion->pir.gpio);
  TEST_ASSERT_TRUE(motion->pir.active_high);
  TEST_ASSERT_EQUAL_UINT32(50, motion->pir.debounce_ms);
  TEST_ASSERT_EQUAL_INT(NODE_HAL_GPIO_PULL_NONE, motion->pir.pull);
}

/* AC: specs/03-config-parser.md #2 */
void test_example_config_fills_in_every_led_setting(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_example());
  const node_config_dev_t *led = &g_config.dev[EXAMPLE_LED];

  TEST_ASSERT_EQUAL_STRING("led1", led->id);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_DEV_LED, led->type);
  TEST_ASSERT_EQUAL_INT(25, led->led.gpio);
  TEST_ASSERT_TRUE(led->led.active_high);
  TEST_ASSERT_FALSE(led->led.default_on);
}

/* AC: specs/03-config-parser.md #2 */
void test_config_without_i2c_buses_is_accepted_when_no_sensor_needs_one(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25)));

  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, code);
  TEST_ASSERT_EQUAL_INT(0, g_config.n_i2c);
  TEST_ASSERT_EQUAL_INT(1, g_config.n_dev);
}

/* AC: specs/03-config-parser.md #2 */
void test_config_with_an_empty_device_list_is_accepted(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(TEST_BUS_I2C0, ""));

  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, code);
  TEST_ASSERT_EQUAL_INT(0, g_config.n_dev);
}

/* AC: specs/03-config-parser.md #3 */
void test_config_with_the_maximum_of_8_devices_and_2_buses_is_accepted(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(
          TEST_BUS_I2C0 "," TEST_BUS_I2C1,
          TEST_SHTC3("env1", "i2c0") "," TEST_SHTC3("env2", "i2c1") "," TEST_PIR("pir1", 4) "," TEST_PIR(
              "pir2",
              13) "," TEST_PIR("pir3",
                               14) "," TEST_LED("led1",
                                                25) "," TEST_LED("led2",
                                                                 26) "," TEST_LED("led3",
                                                                                  27)));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_MAX_I2C_BUSES, g_config.n_i2c);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_MAX_DEVICES, g_config.n_dev);
  TEST_ASSERT_EQUAL_INT(1, g_config.dev[1].shtc3.bus_index);
}

/* AC: specs/03-config-parser.md #2 */
void test_config_text_without_a_terminating_nul_is_accepted(void) {
  static const char text[] = TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25));
  /* Exactly the JSON bytes, with no NUL after them, as when the config is
   * read from a buffer of a known length. */
  char unterminated[sizeof(text) - 1] = {0};
  (void)memcpy(unterminated, text, sizeof(unterminated));

  node_config_err_code_t code =
      node_config_parse(unterminated, sizeof(unterminated), &g_config, &g_err);

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_INT(1, g_config.n_dev);
}

/* AC: specs/03-config-parser.md #2 */
void test_config_followed_by_blank_space_is_accepted(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25)) " \r\n\t ");

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
}

/* AC: specs/03-config-parser.md #2 */
void test_error_details_are_empty_after_a_config_is_accepted(void) {
  (void)memset(&g_err, 'x', sizeof(g_err));

  node_config_err_code_t code = parse_example();

  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, code);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, g_err.code);
  TEST_ASSERT_EQUAL_STRING("", g_err.path);
  TEST_ASSERT_EQUAL_STRING("", g_err.msg);
}

/* ---- Filling in defaults ---- */

/* AC: specs/03-config-parser.md #2 */
void test_motion_sensor_debounce_defaults_to_50_ms_when_omitted(void) {
  TEST_ASSERT_EQUAL_INT(
      NODE_CONFIG_OK,
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_PIR("pir1", 27))));

  TEST_ASSERT_EQUAL_UINT32(DEFAULT_DEBOUNCE_MS,
                           g_config.dev[0].pir.debounce_ms);
}

/* AC: specs/03-config-parser.md #2 */
void test_motion_sensor_pull_resistor_defaults_to_none_when_omitted(void) {
  TEST_ASSERT_EQUAL_INT(
      NODE_CONFIG_OK,
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_PIR("pir1", 27))));

  TEST_ASSERT_EQUAL_INT(NODE_HAL_GPIO_PULL_NONE, g_config.dev[0].pir.pull);
}

/* AC: specs/03-config-parser.md #2 */
void test_led_is_off_at_boot_when_default_on_is_omitted(void) {
  TEST_ASSERT_EQUAL_INT(
      NODE_CONFIG_OK,
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25))));

  TEST_ASSERT_FALSE(g_config.dev[0].led.default_on);
}

/* AC: specs/03-config-parser.md #2 */
void test_sensor_reports_every_5_to_60_seconds_on_a_0_5_c_or_2_percent_change_when_report_is_omitted(
    void) {
  TEST_ASSERT_EQUAL_INT(
      NODE_CONFIG_OK,
      parse_text(TEST_CONFIG(TEST_BUS_I2C0, TEST_SHTC3("env1", "i2c0"))));
  const node_config_report_t *report = &g_config.dev[0].shtc3.report;

  TEST_ASSERT_EQUAL_UINT32(DEFAULT_MIN_INTERVAL_MS, report->min_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(DEFAULT_MAX_INTERVAL_MS, report->max_interval_ms);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_DELTA_TEMP_C, report->delta_temp_c);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_DELTA_RH_PCT, report->delta_rh_pct);
}

/* AC: specs/03-config-parser.md #2 */
void test_omitted_report_fields_take_defaults_while_given_ones_are_kept(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(
      TEST_BUS_I2C0,
      "{\"id\":\"env1\",\"type\":\"shtc3\",\"bus\":\"i2c0\","
      "\"sample_interval_ms\":10000,"
      "\"report\":{\"max_interval_ms\":30000,\"delta_rh_pct\":5.0}}"));
  const node_config_report_t *report = &g_config.dev[0].shtc3.report;

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_UINT32(DEFAULT_MIN_INTERVAL_MS, report->min_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(30000, report->max_interval_ms);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_DELTA_TEMP_C, report->delta_temp_c);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, report->delta_rh_pct);
}

/* ---- Rejecting unusable input ---- */

/* AC: specs/03-config-parser.md #3 */
void test_config_larger_than_4_kb_is_rejected_before_parsing(void) {
  /* A valid config padded with spaces, so only its size is wrong. */
  node_config_err_code_t code = parse_padded_to(NODE_CONFIG_MAX_RAW_LEN + 1);

  assert_rejected(code, NODE_CONFIG_ERR_TOO_LARGE, "");
}

/* AC: specs/03-config-parser.md #3 */
void test_config_of_exactly_4_kb_is_not_rejected_for_its_size(void) {
  node_config_err_code_t code = parse_padded_to(NODE_CONFIG_MAX_RAW_LEN);

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
}

/* AC: specs/03-config-parser.md #3 */
void test_malformed_config_is_rejected_with_the_position_of_the_error(void) {
  /* The unquoted word starts at byte 25, counting from 0. */
  node_config_err_code_t code =
      parse_text("{\"version\":1,\"node_name\":oops}");

  assert_rejected(code, NODE_CONFIG_ERR_SYNTAX, "");
  TEST_ASSERT_EQUAL_STRING("malformed JSON near byte 25", g_err.msg);
}

/* AC: specs/03-config-parser.md #3 */
void test_config_with_text_after_its_closing_brace_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25)) " extra");

  assert_rejected(code, NODE_CONFIG_ERR_SYNTAX, "");
}

/* AC: specs/03-config-parser.md #3 */
void test_empty_config_text_is_rejected(void) {
  node_config_err_code_t code = parse_text("");

  assert_rejected(code, NODE_CONFIG_ERR_SYNTAX, "");
}

/* AC: specs/03-config-parser.md #3 */
void test_config_that_is_not_a_json_object_is_rejected(void) {
  node_config_err_code_t code = parse_text("[1, 2, 3]");

  assert_rejected(code, NODE_CONFIG_ERR_TYPE, "");
}

void test_missing_config_text_is_rejected_as_a_caller_error(void) {
  node_config_err_code_t code =
      node_config_parse(NULL, sizeof(g_example_config), &g_config, &g_err);

  assert_rejected(code, NODE_CONFIG_ERR_ARG, "");
}

void test_missing_output_is_rejected_as_a_caller_error(void) {
  node_config_err_code_t code = node_config_parse(
      g_example_config, sizeof(g_example_config), NULL, &g_err);

  assert_rejected(code, NODE_CONFIG_ERR_ARG, "");
}

void test_config_is_checked_even_when_the_caller_wants_no_error_details(void) {
  static const char text[] = CONFIG_WITH_VERSION(2, "\"node_name\":\"n\"");

  node_config_err_code_t code =
      node_config_parse(text, strlen(text), &g_config, NULL);

  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_ERR_VERSION, code);
}

void test_output_is_cleared_when_a_config_is_rejected(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_example());

  /* Two LEDs with one id parse fully and fail only the final checks, so
   * the output was filled in before the config was rejected. */
  node_config_err_code_t code = parse_text(
      TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25) "," TEST_LED("led1", 26)));

  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_ERR_DUPLICATE, code);
  TEST_ASSERT_EACH_EQUAL_HEX8(0, &g_config, sizeof(g_config));
}

/* ---- Rejecting invalid fields ---- */

/* AC: specs/03-config-parser.md #3 */
void test_config_with_a_version_other_than_1_is_rejected(void) {
  node_config_err_code_t code = parse_text(
      CONFIG_WITH_VERSION(2, "\"node_name\":\"test-node\",\"devices\":[]"));

  assert_rejected(code, NODE_CONFIG_ERR_VERSION, "version");
}

/* AC: specs/03-config-parser.md #3 */
void test_config_without_a_version_is_rejected(void) {
  node_config_err_code_t code =
      parse_text("{\"node_name\":\"test-node\",\"devices\":[]}");

  assert_rejected(code, NODE_CONFIG_ERR_MISSING, "version");
}

/* AC: specs/03-config-parser.md #3 */
void test_config_without_a_node_name_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(CONFIG_WITH_VERSION(1, "\"devices\":[]"));

  assert_rejected(code, NODE_CONFIG_ERR_MISSING, "node_name");
}

/* AC: specs/03-config-parser.md #3 */
void test_node_name_longer_than_31_characters_is_rejected(void) {
  node_config_err_code_t code = parse_text(CONFIG_WITH_VERSION(
      1, "\"node_name\":\"" NAME_OF_32_CHARACTERS "\",\"devices\":[]"));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "node_name");
}

/* AC: specs/03-config-parser.md #3 */
void test_node_name_of_31_characters_is_accepted(void) {
  node_config_err_code_t code = parse_text(CONFIG_WITH_VERSION(
      1, "\"node_name\":\"" NAME_OF_31_CHARACTERS "\",\"devices\":[]"));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_STRING(NAME_OF_31_CHARACTERS, g_config.node_name);
}

/* AC: specs/03-config-parser.md #3 */
void test_device_id_of_16_characters_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED(ID_OF_16_CHARACTERS, 25)));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "devices[0].id");
}

/* AC: specs/03-config-parser.md #3 */
void test_device_id_of_15_characters_is_accepted(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED(ID_OF_15_CHARACTERS, 25)));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_STRING(ID_OF_15_CHARACTERS, g_config.dev[0].id);
}

/* AC: specs/03-config-parser.md #3 */
void test_empty_device_id_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("", 25)));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "devices[0].id");
}

/* AC: specs/03-config-parser.md #3 */
void test_config_without_a_device_list_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(CONFIG_WITH_VERSION(1, "\"node_name\":\"test-node\""));

  assert_rejected(code, NODE_CONFIG_ERR_MISSING, "devices");
}

/* AC: specs/03-config-parser.md #3 */
void test_more_than_8_devices_are_rejected(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG_WITHOUT_BUSES(
      TEST_LED("led1", 4) "," TEST_LED("led2", 13) "," TEST_LED("led3", 14) "," TEST_LED("led4", 18) "," TEST_LED(
          "led5",
          19) "," TEST_LED("led6",
                           21) "," TEST_LED("led7",
                                            22) "," TEST_LED("led8",
                                                             23) "," TEST_LED("led9",
                                                                              25)));

  assert_rejected(code, NODE_CONFIG_ERR_TOO_MANY, "devices");
}

/* AC: specs/03-config-parser.md #3 */
void test_more_than_2_i2c_buses_are_rejected(void) {
  node_config_err_code_t code = parse_text(
      TEST_CONFIG(TEST_BUS_I2C0 "," TEST_BUS_I2C1 "," TEST_BUS_I2C1, ""));

  assert_rejected(code, NODE_CONFIG_ERR_TOO_MANY, "i2c_buses");
}

/* AC: specs/03-config-parser.md #3 */
void test_device_with_an_unknown_type_is_rejected_and_names_the_bad_entry(
    void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25) "," TEST_PIR(
          "pir1", 27) ","
                      "{\"id\":\"air1\",\"type\":\"bme280\"}"));

  assert_rejected(code, NODE_CONFIG_ERR_DEVICE_TYPE, "devices[2].type");
  TEST_ASSERT_EQUAL_STRING("devices[2].type: unknown device type \"bme280\"",
                           g_err.msg);
}

/* AC: specs/03-config-parser.md #3 */
void test_device_without_a_type_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES("{\"id\":\"led1\",\"gpio\":25}"));

  assert_rejected(code, NODE_CONFIG_ERR_MISSING, "devices[0].type");
}

/* AC: specs/03-config-parser.md #3 */
void test_sensor_naming_an_i2c_bus_that_does_not_exist_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(TEST_BUS_I2C0, TEST_SHTC3("env1", "i2c9")));

  assert_rejected(code, NODE_CONFIG_ERR_BUS_REF, "devices[0].bus");
}

/* AC: specs/03-config-parser.md #3 */
void test_i2c_bus_slower_than_10_khz_is_rejected(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(BUS_AT_HZ(9999), ""));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "i2c_buses[0].freq_hz");
}

/* AC: specs/03-config-parser.md #3 */
void test_i2c_bus_faster_than_400_khz_is_rejected(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(BUS_AT_HZ(400001), ""));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "i2c_buses[0].freq_hz");
}

/* AC: specs/03-config-parser.md #3 */
void test_i2c_bus_at_exactly_10_khz_or_400_khz_is_accepted(void) {
  node_config_err_code_t slowest =
      parse_text(TEST_CONFIG(BUS_AT_HZ(10000), ""));
  node_config_err_code_t fastest =
      parse_text(TEST_CONFIG(BUS_AT_HZ(400000), ""));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, slowest, "10 kHz");
  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, fastest, "400 kHz");
}

/* AC: specs/03-config-parser.md #3 */
void test_i2c_port_other_than_0_or_1_is_rejected(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(BUS_ON_PORT(2), ""));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "i2c_buses[0].port");
}

/* AC: specs/03-config-parser.md #3 */
void test_sensor_sampling_more_often_than_once_a_second_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(TEST_BUS_I2C0, SHTC3_SAMPLING_EVERY(999)));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "devices[0].sample_interval_ms");
}

/* AC: specs/03-config-parser.md #3 */
void test_sensor_sampling_exactly_once_a_second_is_accepted(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(TEST_BUS_I2C0, SHTC3_SAMPLING_EVERY(1000)));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_UINT32(1000, g_config.dev[0].shtc3.sample_interval_ms);
}

/* AC: specs/03-config-parser.md #3 */
void test_motion_sensor_with_an_unknown_pull_resistor_setting_is_rejected(
    void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG_WITHOUT_BUSES(
      "{\"id\":\"pir1\",\"type\":\"pir\",\"gpio\":27,\"active_high\":true,"
      "\"pull\":\"sideways\"}"));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "devices[0].pull");
}

/* AC: specs/03-config-parser.md #3 */
void test_number_with_a_fraction_where_a_whole_number_is_expected_is_rejected(
    void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG_WITHOUT_BUSES(
      "{\"id\":\"led1\",\"type\":\"led\",\"gpio\":25.5,\"active_high\":true}"));

  assert_rejected(code, NODE_CONFIG_ERR_TYPE, "devices[0].gpio");
}

/* AC: specs/03-config-parser.md #3 */
void test_text_where_a_number_is_expected_is_rejected(void) {
  node_config_err_code_t code = parse_text(
      TEST_CONFIG_WITHOUT_BUSES("{\"id\":\"led1\",\"type\":\"led\",\"gpio\":"
                                "\"25\",\"active_high\":true}"));

  assert_rejected(code, NODE_CONFIG_ERR_TYPE, "devices[0].gpio");
}

/* AC: specs/03-config-parser.md #3 */
void test_number_where_true_or_false_is_expected_is_rejected(void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG_WITHOUT_BUSES(
      "{\"id\":\"led1\",\"type\":\"led\",\"gpio\":25,\"active_high\":1}"));

  assert_rejected(code, NODE_CONFIG_ERR_TYPE, "devices[0].active_high");
}

/* AC: specs/03-config-parser.md #3 */
void test_negative_gpio_number_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", -1)));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE, "devices[0].gpio");
}

/* ---- Warnings ---- */

/* AC: specs/03-config-parser.md #3 */
void test_unknown_key_is_ignored_with_a_warning_that_names_it(void) {
  node_config_err_code_t code = parse_text(CONFIG_WITH_VERSION(
      1, "\"node_name\":\"test-node\",\"colour\":\"blue\",\"devices\":[]"));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_size_t(1, node_config_log_mock_count());
  TEST_ASSERT_EQUAL_STRING("colour", node_config_log_mock_path(0));
}

/* AC: specs/03-config-parser.md #3 */
void test_unknown_key_inside_a_device_is_warned_about_with_its_full_path(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(LED_WITH("\"brightness\":5")));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_size_t(1, node_config_log_mock_count());
  TEST_ASSERT_EQUAL_STRING("devices[0].brightness",
                           node_config_log_mock_path(0));
}

/* AC: specs/03-config-parser.md #3 */
void test_key_that_belongs_to_another_device_type_is_warned_about(void) {
  /* debounce_ms is a motion sensor setting; an LED has no use for it. */
  node_config_err_code_t code =
      parse_text(TEST_CONFIG_WITHOUT_BUSES(LED_WITH("\"debounce_ms\":50")));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  TEST_ASSERT_EQUAL_size_t(1, node_config_log_mock_count());
  TEST_ASSERT_EQUAL_STRING("devices[0].debounce_ms",
                           node_config_log_mock_path(0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_example_config_is_accepted_without_warnings);
  RUN_TEST(test_example_config_fills_in_the_node_name_and_i2c_bus_settings);
  RUN_TEST(test_example_config_fills_in_every_temperature_sensor_setting);
  RUN_TEST(test_example_config_fills_in_every_motion_sensor_setting);
  RUN_TEST(test_example_config_fills_in_every_led_setting);
  RUN_TEST(test_config_without_i2c_buses_is_accepted_when_no_sensor_needs_one);
  RUN_TEST(test_config_with_an_empty_device_list_is_accepted);
  RUN_TEST(test_config_with_the_maximum_of_8_devices_and_2_buses_is_accepted);
  RUN_TEST(test_config_text_without_a_terminating_nul_is_accepted);
  RUN_TEST(test_config_followed_by_blank_space_is_accepted);
  RUN_TEST(test_error_details_are_empty_after_a_config_is_accepted);
  RUN_TEST(test_motion_sensor_debounce_defaults_to_50_ms_when_omitted);
  RUN_TEST(test_motion_sensor_pull_resistor_defaults_to_none_when_omitted);
  RUN_TEST(test_led_is_off_at_boot_when_default_on_is_omitted);
  RUN_TEST(
      test_sensor_reports_every_5_to_60_seconds_on_a_0_5_c_or_2_percent_change_when_report_is_omitted);
  RUN_TEST(test_omitted_report_fields_take_defaults_while_given_ones_are_kept);
  RUN_TEST(test_config_larger_than_4_kb_is_rejected_before_parsing);
  RUN_TEST(test_config_of_exactly_4_kb_is_not_rejected_for_its_size);
  RUN_TEST(test_malformed_config_is_rejected_with_the_position_of_the_error);
  RUN_TEST(test_config_with_text_after_its_closing_brace_is_rejected);
  RUN_TEST(test_empty_config_text_is_rejected);
  RUN_TEST(test_config_that_is_not_a_json_object_is_rejected);
  RUN_TEST(test_missing_config_text_is_rejected_as_a_caller_error);
  RUN_TEST(test_missing_output_is_rejected_as_a_caller_error);
  RUN_TEST(test_config_is_checked_even_when_the_caller_wants_no_error_details);
  RUN_TEST(test_output_is_cleared_when_a_config_is_rejected);
  RUN_TEST(test_config_with_a_version_other_than_1_is_rejected);
  RUN_TEST(test_config_without_a_version_is_rejected);
  RUN_TEST(test_config_without_a_node_name_is_rejected);
  RUN_TEST(test_node_name_longer_than_31_characters_is_rejected);
  RUN_TEST(test_node_name_of_31_characters_is_accepted);
  RUN_TEST(test_device_id_of_16_characters_is_rejected);
  RUN_TEST(test_device_id_of_15_characters_is_accepted);
  RUN_TEST(test_empty_device_id_is_rejected);
  RUN_TEST(test_config_without_a_device_list_is_rejected);
  RUN_TEST(test_more_than_8_devices_are_rejected);
  RUN_TEST(test_more_than_2_i2c_buses_are_rejected);
  RUN_TEST(
      test_device_with_an_unknown_type_is_rejected_and_names_the_bad_entry);
  RUN_TEST(test_device_without_a_type_is_rejected);
  RUN_TEST(test_sensor_naming_an_i2c_bus_that_does_not_exist_is_rejected);
  RUN_TEST(test_i2c_bus_slower_than_10_khz_is_rejected);
  RUN_TEST(test_i2c_bus_faster_than_400_khz_is_rejected);
  RUN_TEST(test_i2c_bus_at_exactly_10_khz_or_400_khz_is_accepted);
  RUN_TEST(test_i2c_port_other_than_0_or_1_is_rejected);
  RUN_TEST(test_sensor_sampling_more_often_than_once_a_second_is_rejected);
  RUN_TEST(test_sensor_sampling_exactly_once_a_second_is_accepted);
  RUN_TEST(
      test_motion_sensor_with_an_unknown_pull_resistor_setting_is_rejected);
  RUN_TEST(
      test_number_with_a_fraction_where_a_whole_number_is_expected_is_rejected);
  RUN_TEST(test_text_where_a_number_is_expected_is_rejected);
  RUN_TEST(test_number_where_true_or_false_is_expected_is_rejected);
  RUN_TEST(test_negative_gpio_number_is_rejected);
  RUN_TEST(test_unknown_key_is_ignored_with_a_warning_that_names_it);
  RUN_TEST(test_unknown_key_inside_a_device_is_warned_about_with_its_full_path);
  RUN_TEST(test_key_that_belongs_to_another_device_type_is_warned_about);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_config_err_code_t parse_text(const char *json) {
  return node_config_parse(json, strlen(json), &g_config, &g_err);
}

static node_config_err_code_t parse_example(void) {
  return node_config_parse(g_example_config, sizeof(g_example_config),
                           &g_config, &g_err);
}

/* Copies the example config to the start of a buffer of @p len bytes, padded
 * with spaces, and parses it. */
static node_config_err_code_t parse_padded_to(size_t len) {
  static char padded[NODE_CONFIG_MAX_RAW_LEN + 1] = {0};
  TEST_ASSERT_TRUE(len <= sizeof(padded));
  TEST_ASSERT_TRUE(sizeof(g_example_config) <= len);
  (void)memset(padded, ' ', sizeof(padded));
  (void)memcpy(padded, g_example_config, sizeof(g_example_config));
  return node_config_parse(padded, len, &g_config, &g_err);
}

/* A rejection must name the bad value: the message starts with its JSON path,
 * or, for errors about the whole input, is at least not empty. */
static void assert_rejected(node_config_err_code_t code,
                            node_config_err_code_t expected_code,
                            const char *expected_path) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(expected_code, code, g_err.msg);
  TEST_ASSERT_EQUAL_INT(expected_code, g_err.code);
  TEST_ASSERT_EQUAL_STRING(expected_path, g_err.path);
  TEST_ASSERT_TRUE_MESSAGE(g_err.msg[0] != '\0', "error message is empty");
  TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(expected_path, g_err.msg,
                                       strlen(expected_path),
                                       "error message does not start with "
                                       "the JSON path");
}
