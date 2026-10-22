/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#include "node_config.h"
#include "node_config_example.h"
#include "node_config_log_mock.h"
#include "node_config_test_configs.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Hash of the example config's composition (SHTC3, PIR, LED). Spec 10
 *         stores this value in flash at provisioning, so it must never change
 *         between firmware versions. */
#define EXAMPLE_COMPOSITION_HASH 0xc3c4b1b5u

/** @brief A sensor, a motion sensor and an LED, in that order. */
#define SENSOR_MOTION_LED                                             \
  TEST_CONFIG(TEST_BUS_I2C0, TEST_SHTC3("env1", "i2c0") "," TEST_PIR( \
                                 "pir1", 27) "," TEST_LED("led1", 25))

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Config filled by the last hash_of() call. */
static node_config_t g_config = {0};

/** @brief Error details of the last hash_of() call. */
static node_config_err_t g_err = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

static uint32_t hash_of(const char *json, size_t len);
static uint32_t hash_of_text(const char *json);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_config_log_mock_reset();
  (void)memset(&g_config, 0, sizeof(g_config));
  (void)memset(&g_err, 0, sizeof(g_err));
}

void tearDown(void) {}

/* ---- Detecting composition changes ---- */

/* AC: specs/03-config-parser.md #6 */
void test_composition_hash_is_unchanged_when_intervals_change(void) {
  uint32_t original = hash_of_text(SENSOR_MOTION_LED);

  uint32_t retimed = hash_of_text(TEST_CONFIG(
      TEST_BUS_I2C0,
      "{\"id\":\"env1\",\"type\":\"shtc3\",\"bus\":\"i2c0\","
      "\"sample_interval_ms\":2000,\"report\":{\"min_interval_ms\":1000,"
      "\"max_interval_ms\":2000,\"delta_temp_c\":0.1}},"
      "{\"id\":\"pir1\",\"type\":\"pir\",\"gpio\":27,\"active_high\":true,"
      "\"debounce_ms\":200}," TEST_LED("led1", 25)));

  TEST_ASSERT_EQUAL_HEX32(original, retimed);
}

/* AC: specs/03-config-parser.md #6 */
void test_composition_hash_is_unchanged_when_ids_or_pins_change(void) {
  uint32_t original = hash_of_text(SENSOR_MOTION_LED);

  uint32_t rewired = hash_of_text(TEST_CONFIG(
      TEST_BUS_I2C1, TEST_SHTC3("air", "i2c1") "," TEST_PIR(
                         "hallway", 14) "," TEST_LED("status", 26)));

  TEST_ASSERT_EQUAL_HEX32(original, rewired);
}

/* AC: specs/03-config-parser.md #6 */
void test_composition_hash_changes_when_the_device_order_changes(void) {
  uint32_t original = hash_of_text(SENSOR_MOTION_LED);

  uint32_t reordered = hash_of_text(TEST_CONFIG(
      TEST_BUS_I2C0, TEST_LED("led1", 25) "," TEST_PIR(
                         "pir1", 27) "," TEST_SHTC3("env1", "i2c0")));

  TEST_ASSERT_NOT_EQUAL_UINT32(original, reordered);
}

/* AC: specs/03-config-parser.md #6 */
void test_composition_hash_changes_when_a_device_type_changes(void) {
  uint32_t original = hash_of_text(SENSOR_MOTION_LED);

  /* The motion sensor in the middle becomes a second LED. */
  uint32_t retyped = hash_of_text(
      TEST_CONFIG(TEST_BUS_I2C0, TEST_SHTC3("env1", "i2c0") "," TEST_LED(
                                     "led2", 27) "," TEST_LED("led1", 25)));

  TEST_ASSERT_NOT_EQUAL_UINT32(original, retyped);
}

/* AC: specs/03-config-parser.md #6 */
void test_composition_hash_changes_when_a_device_is_added(void) {
  uint32_t original = hash_of_text(SENSOR_MOTION_LED);

  uint32_t extended = hash_of_text(TEST_CONFIG(
      TEST_BUS_I2C0,
      TEST_SHTC3("env1", "i2c0") "," TEST_PIR("pir1", 27) "," TEST_LED(
          "led1", 25) "," TEST_LED("led2", 26)));

  TEST_ASSERT_NOT_EQUAL_UINT32(original, extended);
}

/* AC: specs/03-config-parser.md #6 */
void test_composition_hash_of_the_example_config_stays_at_its_released_value(
    void) {
  uint32_t hash = hash_of(g_example_config, sizeof(g_example_config));

  TEST_ASSERT_EQUAL_HEX32(EXAMPLE_COMPOSITION_HASH, hash);
}

void test_composition_hash_of_a_missing_config_is_zero(void) {
  uint32_t hash = node_config_composition_hash(NULL);

  TEST_ASSERT_EQUAL_HEX32(0, hash);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_composition_hash_is_unchanged_when_intervals_change);
  RUN_TEST(test_composition_hash_is_unchanged_when_ids_or_pins_change);
  RUN_TEST(test_composition_hash_changes_when_the_device_order_changes);
  RUN_TEST(test_composition_hash_changes_when_a_device_type_changes);
  RUN_TEST(test_composition_hash_changes_when_a_device_is_added);
  RUN_TEST(
      test_composition_hash_of_the_example_config_stays_at_its_released_value);
  RUN_TEST(test_composition_hash_of_a_missing_config_is_zero);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

/* The config must be accepted; a hash of a rejected config means nothing. */
static uint32_t hash_of(const char *json, size_t len) {
  node_config_err_code_t code = node_config_parse(json, len, &g_config, &g_err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_CONFIG_OK, code, g_err.msg);
  return node_config_composition_hash(&g_config);
}

static uint32_t hash_of_text(const char *json) {
  return hash_of(json, strlen(json));
}
