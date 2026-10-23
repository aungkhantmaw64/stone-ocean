/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdio.h>
#include <string.h>

#include "node_config.h"
#include "node_config_log_mock.h"
#include "node_config_test_configs.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Size of the buffers for configs and messages built at run time. */
#define TEXT_LEN 512

/** @brief Number of elements in a fixed-size array. */
#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/** @brief GPIO of the board's status LED (spec 16). */
#define STATUS_LED_PIN 0

/** @brief GPIO of the board's reset button (spec 16). */
#define RESET_BUTTON_PIN 34

/** @brief A second I2C bus on port 0, which "i2c0" already uses. */
#define SECOND_BUS_ON_PORT_0 \
  "{\"id\":\"i2c1\",\"port\":0,\"sda\":18,\"scl\":19,\"freq_hz\":100000}"

/** @brief SHTC3 on bus "i2c0" with the given "report" members. */
#define SHTC3_REPORTING(members)                          \
  "{\"id\":\"env1\",\"type\":\"shtc3\",\"bus\":\"i2c0\"," \
  "\"sample_interval_ms\":10000,\"report\":{" members "}}"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Config filled by the last parse call. */
static node_config_t g_config = {0};

/** @brief Error details of the last parse call. */
static node_config_err_t g_err = {0};

/** @brief GPIOs the flash chip is wired to on the WROVER module. */
static const int g_flash_pins[] = {6, 7, 8, 9, 10, 11};

/** @brief GPIOs the PSRAM uses on the WROVER module. */
static const int g_psram_pins[] = {16, 17};

/** @brief GPIOs of UART0, the serial console. */
static const int g_console_pins[] = {1, 3};

/** @brief GPIO numbers below 40 that the ESP32 does not have. */
static const int g_missing_pins[] = {24, 28, 29, 30, 31};

/** @brief GPIO numbers above the highest one, 39. */
static const int g_pins_above_39[] = {40, 41, 100};

/** @brief GPIOs that have no output driver and no pull resistors; GPIO 34,
 *         the reset button's, is left out. */
static const int g_input_only_pins[] = {35, 36, 37, 38, 39};

/** @brief Every GPIO an LED may use; GPIO 0 is the status LED's. */
static const int g_usable_output_pins[] = {2,  4,  5,  12, 13, 14, 15, 18, 19,
                                           20, 21, 22, 23, 25, 26, 27, 32, 33};

/** @brief Pins that set the boot mode at reset and a config may use; GPIO 0
 *         is the status LED's. */
static const int g_strapping_pins[] = {2, 5, 12, 15};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Parse a NUL-terminated config into g_config and g_err.
 *
 * @param json Config text.
 * @return The result of node_config_parse().
 */
static node_config_err_code_t parse_text(const char *json);

/**
 * @brief Parse a config with one LED on @p gpio and no I2C buses.
 *
 * @param gpio GPIO number of the LED.
 * @return The result of node_config_parse().
 */
static node_config_err_code_t parse_led_on(int gpio);

/**
 * @brief Parse a config with one PIR sensor on @p gpio and no I2C buses.
 *
 * @param gpio GPIO number of the sensor.
 * @param pull Value of "pull": "none", "up" or "down".
 * @return The result of node_config_parse().
 */
static node_config_err_code_t parse_motion_sensor_on(int gpio,
                                                     const char *pull);

/**
 * @brief Parse a config with one I2C bus on @p sda and @p scl.
 *
 * @param sda GPIO number of SDA.
 * @param scl GPIO number of SCL.
 * @return The result of node_config_parse().
 */
static node_config_err_code_t parse_bus_on(int sda, int scl);

/**
 * @brief Check that a parse failed with @p expected_code and that g_err names
 *        @p expected_path, with a message starting with it.
 *
 * @param code          Result of the parse.
 * @param expected_code Expected error code.
 * @param expected_path Expected JSON path.
 */
static void assert_rejected(node_config_err_code_t code,
                            node_config_err_code_t expected_code,
                            const char *expected_path);

/**
 * @brief Check that an LED on each of @p pins is rejected with the message
 *        "devices[0].gpio: GPIO <pin> <reason>".
 *
 * @param pins   GPIO numbers to try.
 * @param n_pins Number of entries in @p pins.
 * @param reason Expected end of the message, for example "is input-only".
 */
static void assert_led_pins_rejected(const int *pins, size_t n_pins,
                                     const char *reason);

/**
 * @brief Check that @p pin is rejected in every pin field (LED, motion sensor,
 *        I2C data and clock) with the message
 *        "<path>: GPIO <pin> is used by <owner>".
 *
 * @param pin   GPIO number the board reserves.
 * @param owner Board part that uses the pin, for example "the status LED".
 */
static void assert_board_pin_rejected_in_every_field(int pin,
                                                     const char *owner);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_config_log_mock_reset();
  (void)memset(&g_config, 0, sizeof(g_config));
  (void)memset(&g_err, 0, sizeof(g_err));
}

void tearDown(void) {}

/* ---- Rejecting duplicates ---- */

/* AC: specs/03-config-parser.md #3 */
void test_two_devices_with_the_same_id_are_rejected(void) {
  node_config_err_code_t code = parse_text(
      TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25) "," TEST_LED("led1", 26)));

  assert_rejected(code, NODE_CONFIG_ERR_DUPLICATE, "devices[1].id");
}

/* AC: specs/03-config-parser.md #3 */
void test_device_with_the_same_id_as_an_i2c_bus_is_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(TEST_BUS_I2C0, TEST_LED("i2c0", 25)));

  assert_rejected(code, NODE_CONFIG_ERR_DUPLICATE, "devices[0].id");
}

/* AC: specs/03-config-parser.md #3 */
void test_two_i2c_buses_on_the_same_port_are_rejected(void) {
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(TEST_BUS_I2C0 "," SECOND_BUS_ON_PORT_0, ""));

  assert_rejected(code, NODE_CONFIG_ERR_DUPLICATE, "i2c_buses[1].port");
}

/* AC: specs/03-config-parser.md #3 */
void test_two_devices_on_the_same_gpio_are_rejected(void) {
  node_config_err_code_t code = parse_text(
      TEST_CONFIG_WITHOUT_BUSES(TEST_PIR("pir1", 27) "," TEST_LED("led1", 27)));

  assert_rejected(code, NODE_CONFIG_ERR_DUPLICATE, "devices[1].gpio");
}

/* AC: specs/03-config-parser.md #3 */
void test_device_on_a_gpio_used_by_an_i2c_bus_is_rejected(void) {
  /* TEST_BUS_I2C0 uses GPIO 22 for its clock line. */
  node_config_err_code_t code =
      parse_text(TEST_CONFIG(TEST_BUS_I2C0, TEST_LED("led1", 22)));

  assert_rejected(code, NODE_CONFIG_ERR_DUPLICATE, "devices[0].gpio");
}

/* AC: specs/03-config-parser.md #3 */
void test_i2c_bus_using_one_gpio_for_both_data_and_clock_is_rejected(void) {
  node_config_err_code_t code = parse_bus_on(21, 21);

  assert_rejected(code, NODE_CONFIG_ERR_DUPLICATE, "i2c_buses[0].scl");
}

/* ---- Sensor reporting ---- */

/* AC: specs/03-config-parser.md #3 */
void test_sensor_report_with_its_minimum_interval_above_its_maximum_is_rejected(
    void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(
      TEST_BUS_I2C0,
      SHTC3_REPORTING("\"min_interval_ms\":60001,\"max_interval_ms\":60000")));

  assert_rejected(code, NODE_CONFIG_ERR_RANGE,
                  "devices[0].report.min_interval_ms");
}

/* AC: specs/03-config-parser.md #3 */
void test_sensor_report_with_equal_minimum_and_maximum_intervals_is_accepted(
    void) {
  node_config_err_code_t code = parse_text(TEST_CONFIG(
      TEST_BUS_I2C0,
      SHTC3_REPORTING("\"min_interval_ms\":30000,\"max_interval_ms\":30000")));

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
}

/* ---- Pin rules ---- */

/* AC: specs/03-config-parser.md #3 */
void test_gpio_6_to_11_are_rejected_because_the_flash_uses_them(void) {
  assert_led_pins_rejected(g_flash_pins, COUNT_OF(g_flash_pins),
                           "is used by the flash");
}

/* AC: specs/03-config-parser.md #3 */
void test_gpio_16_and_17_are_rejected_because_the_psram_uses_them(void) {
  assert_led_pins_rejected(g_psram_pins, COUNT_OF(g_psram_pins),
                           "is used by the PSRAM");
}

/* AC: specs/03-config-parser.md #3 */
void test_gpio_1_and_3_are_rejected_because_the_serial_console_uses_them(void) {
  assert_led_pins_rejected(g_console_pins, COUNT_OF(g_console_pins),
                           "is used by the serial console");
}

/* AC: specs/03-config-parser.md #3 */
void test_gpio_24_and_28_to_31_are_rejected_because_they_do_not_exist(void) {
  assert_led_pins_rejected(g_missing_pins, COUNT_OF(g_missing_pins),
                           "does not exist on the ESP32");
}

/* AC: specs/03-config-parser.md #3 */
void test_gpio_above_39_is_rejected(void) {
  assert_led_pins_rejected(g_pins_above_39, COUNT_OF(g_pins_above_39),
                           "does not exist on the ESP32");
}

/* AC: specs/03-config-parser.md #3 */
void test_led_on_an_input_only_gpio_is_rejected(void) {
  assert_led_pins_rejected(g_input_only_pins, COUNT_OF(g_input_only_pins),
                           "is input-only");
}

/* AC: specs/03-config-parser.md #3 */
void test_i2c_bus_on_an_input_only_gpio_is_rejected(void) {
  for (size_t i = 0; i < COUNT_OF(g_input_only_pins); i++) {
    char label[TEXT_LEN] = {0};
    (void)snprintf(label, sizeof(label), "data line on GPIO %d",
                   g_input_only_pins[i]);

    node_config_err_code_t code = parse_bus_on(g_input_only_pins[i], 22);

    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_ERR_PIN, code, label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("i2c_buses[0].sda", g_err.path, label);
  }
}

/* AC: specs/03-config-parser.md #3 */
void test_motion_sensor_on_an_input_only_gpio_is_accepted_without_a_pull_resistor(
    void) {
  for (size_t i = 0; i < COUNT_OF(g_input_only_pins); i++) {
    node_config_err_code_t code =
        parse_motion_sensor_on(g_input_only_pins[i], "none");

    char label[TEXT_LEN] = {0};
    (void)snprintf(label, sizeof(label), "GPIO %d: %s", g_input_only_pins[i],
                   g_err.msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, label);
  }
}

/* AC: specs/03-config-parser.md #3 */
void test_motion_sensor_on_an_input_only_gpio_is_rejected_with_a_pull_resistor(
    void) {
  for (size_t i = 0; i < COUNT_OF(g_input_only_pins); i++) {
    char label[TEXT_LEN] = {0};
    (void)snprintf(label, sizeof(label), "GPIO %d", g_input_only_pins[i]);

    node_config_err_code_t code =
        parse_motion_sensor_on(g_input_only_pins[i], "up");

    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_ERR_PIN, code, label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("devices[0].pull", g_err.path, label);
  }
}

/* AC: specs/03-config-parser.md #3 */
void test_every_usable_gpio_is_accepted_for_an_led(void) {
  for (size_t i = 0; i < COUNT_OF(g_usable_output_pins); i++) {
    node_config_err_code_t code = parse_led_on(g_usable_output_pins[i]);

    char label[TEXT_LEN] = {0};
    (void)snprintf(label, sizeof(label), "GPIO %d: %s", g_usable_output_pins[i],
                   g_err.msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, label);
  }
}

/* AC: specs/03-config-parser.md #3 */
void test_strapping_pin_is_accepted_with_a_warning(void) {
  for (size_t i = 0; i < COUNT_OF(g_strapping_pins); i++) {
    char label[TEXT_LEN] = {0};
    (void)snprintf(label, sizeof(label), "GPIO %d", g_strapping_pins[i]);
    node_config_log_mock_reset();

    node_config_err_code_t code = parse_led_on(g_strapping_pins[i]);

    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, label);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, node_config_log_mock_count(), label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("devices[0].gpio",
                                     node_config_log_mock_path(0), label);
  }
}

/* ---- Rejecting the board's pins ---- */

/* AC: specs/16-status-led.md #4 */
void test_config_using_the_status_led_pin_is_rejected_for_every_pin_field(
    void) {
  assert_board_pin_rejected_in_every_field(STATUS_LED_PIN, "the status LED");
}

/* AC: specs/16-status-led.md #4 */
void test_config_using_the_reset_button_pin_is_rejected_for_every_pin_field(
    void) {
  assert_board_pin_rejected_in_every_field(RESET_BUTTON_PIN,
                                           "the reset button");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_two_devices_with_the_same_id_are_rejected);
  RUN_TEST(test_device_with_the_same_id_as_an_i2c_bus_is_rejected);
  RUN_TEST(test_two_i2c_buses_on_the_same_port_are_rejected);
  RUN_TEST(test_two_devices_on_the_same_gpio_are_rejected);
  RUN_TEST(test_device_on_a_gpio_used_by_an_i2c_bus_is_rejected);
  RUN_TEST(test_i2c_bus_using_one_gpio_for_both_data_and_clock_is_rejected);
  RUN_TEST(
      test_sensor_report_with_its_minimum_interval_above_its_maximum_is_rejected);
  RUN_TEST(
      test_sensor_report_with_equal_minimum_and_maximum_intervals_is_accepted);
  RUN_TEST(test_gpio_6_to_11_are_rejected_because_the_flash_uses_them);
  RUN_TEST(test_gpio_16_and_17_are_rejected_because_the_psram_uses_them);
  RUN_TEST(test_gpio_1_and_3_are_rejected_because_the_serial_console_uses_them);
  RUN_TEST(test_gpio_24_and_28_to_31_are_rejected_because_they_do_not_exist);
  RUN_TEST(test_gpio_above_39_is_rejected);
  RUN_TEST(test_led_on_an_input_only_gpio_is_rejected);
  RUN_TEST(test_i2c_bus_on_an_input_only_gpio_is_rejected);
  RUN_TEST(
      test_motion_sensor_on_an_input_only_gpio_is_accepted_without_a_pull_resistor);
  RUN_TEST(
      test_motion_sensor_on_an_input_only_gpio_is_rejected_with_a_pull_resistor);
  RUN_TEST(test_every_usable_gpio_is_accepted_for_an_led);
  RUN_TEST(test_strapping_pin_is_accepted_with_a_warning);
  RUN_TEST(
      test_config_using_the_status_led_pin_is_rejected_for_every_pin_field);
  RUN_TEST(
      test_config_using_the_reset_button_pin_is_rejected_for_every_pin_field);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_config_err_code_t parse_text(const char *json) {
  return node_config_parse(json, strlen(json), &g_config, &g_err);
}

static node_config_err_code_t parse_led_on(int gpio) {
  char text[TEXT_LEN] = {0};
  int written = snprintf(
      text, sizeof(text),
      TEST_CONFIG_WITHOUT_BUSES("{\"id\":\"led1\",\"type\":\"led\",\"gpio\":%d,"
                                "\"active_high\":true}"),
      gpio);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(text));
  return parse_text(text);
}

static node_config_err_code_t parse_motion_sensor_on(int gpio,
                                                     const char *pull) {
  char text[TEXT_LEN] = {0};
  int written = snprintf(
      text, sizeof(text),
      TEST_CONFIG_WITHOUT_BUSES("{\"id\":\"pir1\",\"type\":\"pir\",\"gpio\":%d,"
                                "\"active_high\":true,\"pull\":\"%s\"}"),
      gpio, pull);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(text));
  return parse_text(text);
}

static node_config_err_code_t parse_bus_on(int sda, int scl) {
  char text[TEXT_LEN] = {0};
  int written =
      snprintf(text, sizeof(text),
               TEST_CONFIG("{\"id\":\"i2c0\",\"port\":0,\"sda\":%d,\"scl\":%d,"
                           "\"freq_hz\":100000}",
                           ""),
               sda, scl);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(text));
  return parse_text(text);
}

/* A rejection must name the bad value: the message starts with its JSON
 * path. */
static void assert_rejected(node_config_err_code_t code,
                            node_config_err_code_t expected_code,
                            const char *expected_path) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(expected_code, code, g_err.msg);
  TEST_ASSERT_EQUAL_INT(expected_code, g_err.code);
  TEST_ASSERT_EQUAL_STRING(expected_path, g_err.path);
  TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(expected_path, g_err.msg,
                                       strlen(expected_path),
                                       "error message does not start with "
                                       "the JSON path");
}

/* Each pin must be rejected for an LED, with a message giving @p reason. */
static void assert_led_pins_rejected(const int *pins, size_t n_pins,
                                     const char *reason) {
  for (size_t i = 0; i < n_pins; i++) {
    char expected_msg[TEXT_LEN] = {0};
    (void)snprintf(expected_msg, sizeof(expected_msg),
                   "devices[0].gpio: GPIO %d %s", pins[i], reason);

    node_config_err_code_t code = parse_led_on(pins[i]);

    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_ERR_PIN, code, expected_msg);
    TEST_ASSERT_EQUAL_STRING(expected_msg, g_err.msg);
  }
}

static void assert_board_pin_rejected_in_every_field(int pin,
                                                     const char *owner) {
  const char *const paths[] = {"devices[0].gpio", "devices[0].gpio",
                               "i2c_buses[0].sda", "i2c_buses[0].scl"};
  for (size_t field = 0; field < COUNT_OF(paths); field++) {
    char expected_msg[TEXT_LEN] = {0};
    (void)snprintf(expected_msg, sizeof(expected_msg),
                   "%s: GPIO %d is used by %s", paths[field], pin, owner);

    /* The other I2C line is on a free pin, so only @p pin can be blamed. */
    node_config_err_code_t code = NODE_CONFIG_OK;
    switch (field) {
      case 0:
        code = parse_led_on(pin);
        break;
      case 1:
        code = parse_motion_sensor_on(pin, "none");
        break;
      case 2:
        code = parse_bus_on(pin, 22);
        break;
      default:
        code = parse_bus_on(21, pin);
        break;
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_ERR_PIN, code, expected_msg);
    TEST_ASSERT_EQUAL_STRING(expected_msg, g_err.msg);
  }
}
