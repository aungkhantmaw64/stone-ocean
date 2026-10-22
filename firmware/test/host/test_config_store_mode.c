/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#include "config_store.h"
#include "config_store_fs_mock.h"
#include "config_store_log_mock.h"
#include "config_store_mode.h"
#include "node_config.h"
#include "node_config_example.h"
#include "node_config_log_mock.h"
#include "node_config_test_configs.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief File the config is stored in on the config partition. */
#define CONFIG_FILE "config.bin"

/** @brief Size of the file header: magic, version, length and CRC32. */
#define HEADER_LEN 16u

#define HEADER_MAGIC_OFFSET 0u   /**< Byte offset of the magic. */
#define HEADER_VERSION_OFFSET 4u /**< Byte offset of the format version. */
#define HEADER_LENGTH_OFFSET 8u  /**< Byte offset of the JSON length. */

/** @brief Header magic, "NCFG" read as a little-endian uint32_t. */
#define HEADER_MAGIC 0x4746434Eu

/** @brief Valid config with one LED. */
#define VALID_CONFIG TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 25))

/** @brief Config whose only device uses GPIO 7, which the flash chip owns. */
#define FLASH_PIN_CONFIG TEST_CONFIG_WITHOUT_BUSES(TEST_LED("led1", 7))

/** @brief JSON path the parser names for FLASH_PIN_CONFIG. */
#define FLASH_PIN_PATH "devices[0].gpio"

/** @brief JSON length an over-sized stored config claims: 5000 bytes, above
 *         the 4096-byte limit but within what the file fake can hold. */
#define OVERSIZED_JSON_LEN 5000u

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Stored file for the over-sized config test: a valid header claiming
 *         OVERSIZED_JSON_LEN bytes, followed by that many bytes. */
static uint8_t g_oversized_file[HEADER_LEN + OVERSIZED_JSON_LEN] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Save a NUL-terminated config and fail the test if saving fails.
 *
 * @param json Config text.
 */
static void store_text(const char *json);

/**
 * @brief Write a little-endian uint32_t into file bytes.
 *
 * @param value Value to write.
 * @param bytes At least 4 bytes.
 */
static void write_u32_le(uint32_t value, uint8_t *bytes);

/**
 * @brief Check that the node is in safe mode for @p reason, logged once.
 *
 * @param reason Expected safe-mode reason.
 */
static void assert_safe_mode_because(const char *reason);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  config_store_fs_mock_reset();
  config_store_log_mock_reset();
  node_config_log_mock_reset();
}

void tearDown(void) {}

/* ---- Starting in normal mode ---- */

/* AC: specs/04-config-storage.md #3 */
void test_node_starts_in_normal_mode_when_a_valid_config_is_stored(void) {
  store_text(VALID_CONFIG);

  config_store_mode_boot();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_MODE_NORMAL, config_store_mode_get());
  TEST_ASSERT_NOT_NULL(config_store_mode_active_config());
  TEST_ASSERT_EQUAL_size_t(1, config_store_fs_mock_init_count());
}

/* AC: specs/04-config-storage.md #3 */
void test_active_config_matches_the_stored_config_in_normal_mode(void) {
  TEST_ASSERT_EQUAL_INT(
      CONFIG_STORE_OK,
      config_store_save(g_example_config, sizeof(g_example_config)));

  config_store_mode_boot();

  const node_config_t *config = config_store_mode_active_config();
  TEST_ASSERT_NOT_NULL(config);
  TEST_ASSERT_EQUAL_STRING("lab-node-1", config->node_name);
  TEST_ASSERT_EQUAL_INT(3, config->n_dev);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_DEV_SHTC3, config->dev[0].type);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_DEV_PIR, config->dev[1].type);
  TEST_ASSERT_EQUAL_INT(NODE_CONFIG_DEV_LED, config->dev[2].type);
}

/* AC: specs/04-config-storage.md #2, #3 */
void test_node_in_normal_mode_has_no_safe_mode_reason_and_logs_no_safe_mode_line(
    void) {
  store_text(VALID_CONFIG);

  config_store_mode_boot();

  TEST_ASSERT_NULL(config_store_mode_safe_reason());
  TEST_ASSERT_EQUAL_size_t(0, config_store_log_mock_safe_mode_count());
  TEST_ASSERT_EQUAL_size_t(1, config_store_log_mock_normal_mode_count());
}

/* AC: specs/04-config-storage.md #3, #4 */
void test_node_switches_to_normal_mode_after_a_valid_config_is_saved_and_the_node_reboots(
    void) {
  config_store_mode_boot();
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_MODE_SAFE, config_store_mode_get());
  store_text(VALID_CONFIG);

  config_store_mode_boot();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_MODE_NORMAL, config_store_mode_get());
  TEST_ASSERT_NOT_NULL(config_store_mode_active_config());
}

/* ---- Starting in safe mode ---- */

/* AC: specs/04-config-storage.md #2 */
void test_node_starts_in_safe_mode_when_no_config_is_stored(void) {
  config_store_mode_boot();

  assert_safe_mode_because("no config stored");
}

/* AC: specs/04-config-storage.md #2 */
void test_node_starts_in_safe_mode_when_the_stored_config_is_corrupt(void) {
  store_text(VALID_CONFIG);
  /* Flip one bit of the first JSON byte, as a worn flash cell might. */
  config_store_fs_mock_file(CONFIG_FILE, NULL)[HEADER_LEN] ^= 0x01u;

  config_store_mode_boot();

  assert_safe_mode_because("stored config is corrupt");
}

/* AC: specs/04-config-storage.md #2 */
void test_node_starts_in_safe_mode_and_names_the_bad_entry_when_the_stored_config_is_invalid(
    void) {
  store_text(FLASH_PIN_CONFIG);

  config_store_mode_boot();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_MODE_SAFE, config_store_mode_get());
  const char *reason = config_store_mode_safe_reason();
  TEST_ASSERT_NOT_NULL(reason);
  /* The wording after the path belongs to the parser (spec 03). */
  TEST_ASSERT_EQUAL_STRING_LEN(FLASH_PIN_PATH ": ", reason,
                               strlen(FLASH_PIN_PATH ": "));
  TEST_ASSERT_EQUAL_size_t(1, config_store_log_mock_safe_mode_count());
  TEST_ASSERT_EQUAL_STRING(reason, config_store_log_mock_safe_mode_reason());
}

/* AC: specs/04-config-storage.md #2 */
void test_node_starts_in_safe_mode_when_the_stored_config_is_too_large(void) {
  /* The CRC field stays zero: the size is rejected before the CRC is
   * checked. */
  write_u32_le(HEADER_MAGIC, &g_oversized_file[HEADER_MAGIC_OFFSET]);
  write_u32_le(CONFIG_STORE_FORMAT_VERSION,
               &g_oversized_file[HEADER_VERSION_OFFSET]);
  write_u32_le(OVERSIZED_JSON_LEN, &g_oversized_file[HEADER_LENGTH_OFFSET]);
  TEST_ASSERT_EQUAL_INT(0,
                        config_store_fs_mock_put(CONFIG_FILE, g_oversized_file,
                                                 sizeof(g_oversized_file)));

  config_store_mode_boot();

  assert_safe_mode_because("stored config is too large");
}

/* AC: specs/04-config-storage.md #2 */
void test_node_starts_in_safe_mode_when_the_config_storage_cannot_be_mounted(
    void) {
  store_text(VALID_CONFIG);
  config_store_fs_mock_set_error(CONFIG_STORE_FS_MOCK_OP_INIT,
                                 CONFIG_STORE_ERR_IO);

  config_store_mode_boot();

  assert_safe_mode_because("config storage unavailable");
}

/* AC: specs/04-config-storage.md #2 */
void test_node_starts_in_safe_mode_when_the_config_storage_cannot_be_read(
    void) {
  store_text(VALID_CONFIG);
  config_store_fs_mock_set_error(CONFIG_STORE_FS_MOCK_OP_READ,
                                 CONFIG_STORE_ERR_IO);

  config_store_mode_boot();

  assert_safe_mode_because("config storage read failed");
}

/* AC: specs/04-config-storage.md #2 */
void test_no_config_is_active_in_safe_mode(void) {
  store_text(VALID_CONFIG);
  config_store_mode_boot();
  TEST_ASSERT_NOT_NULL(config_store_mode_active_config());
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, config_store_erase());

  config_store_mode_boot();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_MODE_SAFE, config_store_mode_get());
  TEST_ASSERT_NULL(config_store_mode_active_config());
}

/* AC: specs/04-config-storage.md #2 */
void test_safe_mode_reason_is_logged_exactly_once_per_boot(void) {
  config_store_mode_boot();
  config_store_mode_boot();

  TEST_ASSERT_EQUAL_size_t(2, config_store_log_mock_safe_mode_count());
  TEST_ASSERT_EQUAL_STRING("no config stored",
                           config_store_log_mock_safe_mode_reason());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_node_starts_in_normal_mode_when_a_valid_config_is_stored);
  RUN_TEST(test_active_config_matches_the_stored_config_in_normal_mode);
  RUN_TEST(
      test_node_in_normal_mode_has_no_safe_mode_reason_and_logs_no_safe_mode_line);
  RUN_TEST(
      test_node_switches_to_normal_mode_after_a_valid_config_is_saved_and_the_node_reboots);
  RUN_TEST(test_node_starts_in_safe_mode_when_no_config_is_stored);
  RUN_TEST(test_node_starts_in_safe_mode_when_the_stored_config_is_corrupt);
  RUN_TEST(
      test_node_starts_in_safe_mode_and_names_the_bad_entry_when_the_stored_config_is_invalid);
  RUN_TEST(test_node_starts_in_safe_mode_when_the_stored_config_is_too_large);
  RUN_TEST(
      test_node_starts_in_safe_mode_when_the_config_storage_cannot_be_mounted);
  RUN_TEST(
      test_node_starts_in_safe_mode_when_the_config_storage_cannot_be_read);
  RUN_TEST(test_no_config_is_active_in_safe_mode);
  RUN_TEST(test_safe_mode_reason_is_logged_exactly_once_per_boot);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void store_text(const char *json) {
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, config_store_save(json, strlen(json)));
}

static void write_u32_le(uint32_t value, uint8_t *bytes) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static void assert_safe_mode_because(const char *reason) {
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_MODE_SAFE, config_store_mode_get());
  TEST_ASSERT_NULL(config_store_mode_active_config());
  TEST_ASSERT_EQUAL_STRING(reason, config_store_mode_safe_reason());
  TEST_ASSERT_EQUAL_size_t(1, config_store_log_mock_safe_mode_count());
  TEST_ASSERT_EQUAL_STRING(reason, config_store_log_mock_safe_mode_reason());
}
