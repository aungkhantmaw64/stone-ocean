/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

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
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_text(TEST_CONFIG_WITHOUT_BUSES(
                                            TEST_PIR("pir1", 27))));

  TEST_ASSERT_EQUAL_UINT32(DEFAULT_DEBOUNCE_MS,
                           g_config.dev[0].pir.debounce_ms);
}

/* AC: specs/03-config-parser.md #2 */
void test_motion_sensor_pull_resistor_defaults_to_none_when_omitted(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_text(TEST_CONFIG_WITHOUT_BUSES(
                                            TEST_PIR("pir1", 27))));

  TEST_ASSERT_EQUAL_INT(NODE_HAL_GPIO_PULL_NONE, g_config.dev[0].pir.pull);
}

/* AC: specs/03-config-parser.md #2 */
void test_led_is_off_at_boot_when_default_on_is_omitted(void) {
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_OK, parse_text(TEST_CONFIG_WITHOUT_BUSES(
                                            TEST_LED("led1", 25))));

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
