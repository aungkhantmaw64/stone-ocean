/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#include "config_store.h"
#include "config_store_crc.h"
#include "config_store_fs_mock.h"
#include "node_config.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief File the config is stored in on the config partition. */
#define CONFIG_FILE "config.bin"

/** @brief Temporary file an interrupted save can leave behind. */
#define TMP_FILE "config.tmp"

/** @brief Size of the file header: magic, version, length and CRC32. */
#define HEADER_LEN 16u

#define HEADER_MAGIC_OFFSET 0u   /**< Byte offset of the magic. */
#define HEADER_VERSION_OFFSET 4u /**< Byte offset of the format version. */
#define HEADER_LENGTH_OFFSET 8u  /**< Byte offset of the JSON length. */
#define HEADER_CRC_OFFSET 12u    /**< Byte offset of the JSON CRC32. */

/** @brief Header magic, "NCFG" read as a little-endian uint32_t. */
#define HEADER_MAGIC 0x4746434Eu

/** @brief A small config; storage does not parse it, so any bytes do. */
#define SMALL_CONFIG "{\"version\":1,\"node_name\":\"first\"}"

/** @brief A different small config, to tell two saves apart. */
#define OTHER_CONFIG "{\"version\":1,\"node_name\":\"second\"}"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Buffer configs are loaded into; one byte larger than the limit so
 *         an over-long load would be visible. */
static char g_loaded[NODE_CONFIG_MAX_RAW_LEN + 1] = {0};

/** @brief Number of bytes returned by the last load. */
static size_t g_loaded_len = 0;

/** @brief Config one byte over the limit; filled in setUp(). */
static char g_oversized[NODE_CONFIG_MAX_RAW_LEN + 1] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Save a NUL-terminated config and fail the test if saving fails.
 *
 * @param json Config text.
 */
static void save_text(const char *json);

/**
 * @brief Load the stored config into g_loaded.
 *
 * @return The result of config_store_load().
 */
static config_store_err_t load(void);

/**
 * @brief Read a little-endian uint32_t from stored file bytes.
 *
 * @param bytes At least 4 bytes.
 * @return The value.
 */
static uint32_t read_u32_le(const uint8_t *bytes);

/**
 * @brief Write a little-endian uint32_t into stored file bytes.
 *
 * @param value Value to write.
 * @param bytes At least 4 bytes.
 */
static void write_u32_le(uint32_t value, uint8_t *bytes);

/**
 * @brief Stored config file bytes; fails the test if nothing is stored.
 *
 * @param out_len Receives the file size; may be NULL.
 * @return Writable file contents.
 */
static uint8_t *stored_file(size_t *out_len);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  config_store_fs_mock_reset();
  (void)memset(g_loaded, 0, sizeof(g_loaded));
  g_loaded_len = 0;
  (void)memset(g_oversized, 'x', sizeof(g_oversized));
}

void tearDown(void) {}

/* ---- Saving and loading ---- */

/* AC: specs/04-config-storage.md #1 */
void test_config_that_was_saved_is_loaded_back_byte_for_byte(void) {
  save_text(SMALL_CONFIG);

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, err);
  TEST_ASSERT_EQUAL_size_t(strlen(SMALL_CONFIG), g_loaded_len);
  TEST_ASSERT_EQUAL_MEMORY(SMALL_CONFIG, g_loaded, strlen(SMALL_CONFIG));
}

/* AC: specs/04-config-storage.md #1 */
void test_config_of_exactly_4_kb_is_saved_and_loaded_back(void) {
  config_store_err_t save_err =
      config_store_save(g_oversized, NODE_CONFIG_MAX_RAW_LEN);

  config_store_err_t load_err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, save_err);
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, load_err);
  TEST_ASSERT_EQUAL_size_t(NODE_CONFIG_MAX_RAW_LEN, g_loaded_len);
  TEST_ASSERT_EQUAL_MEMORY(g_oversized, g_loaded, NODE_CONFIG_MAX_RAW_LEN);
}

/* AC: specs/04-config-storage.md #1 */
void test_saving_a_new_config_replaces_the_previous_one(void) {
  save_text(SMALL_CONFIG);

  save_text(OTHER_CONFIG);

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, load());
  TEST_ASSERT_EQUAL_size_t(strlen(OTHER_CONFIG), g_loaded_len);
  TEST_ASSERT_EQUAL_MEMORY(OTHER_CONFIG, g_loaded, strlen(OTHER_CONFIG));
}

/* AC: specs/04-config-storage.md #1 */
void test_saved_config_is_stored_with_a_header_giving_its_length_and_checksum_in_little_endian_byte_order(
    void) {
  save_text(SMALL_CONFIG);

  size_t file_len = 0;
  const uint8_t *file = stored_file(&file_len);

  TEST_ASSERT_EQUAL_size_t(HEADER_LEN + strlen(SMALL_CONFIG), file_len);
  /* "NCFG" on flash, in reading order, because the magic is little-endian. */
  TEST_ASSERT_EQUAL_MEMORY("NCFG", &file[HEADER_MAGIC_OFFSET], 4);
  TEST_ASSERT_EQUAL_HEX32(HEADER_MAGIC,
                          read_u32_le(&file[HEADER_MAGIC_OFFSET]));
  TEST_ASSERT_EQUAL_UINT32(CONFIG_STORE_FORMAT_VERSION,
                           read_u32_le(&file[HEADER_VERSION_OFFSET]));
  TEST_ASSERT_EQUAL_UINT32(strlen(SMALL_CONFIG),
                           read_u32_le(&file[HEADER_LENGTH_OFFSET]));
  TEST_ASSERT_EQUAL_HEX32(
      config_store_crc_compute(SMALL_CONFIG, strlen(SMALL_CONFIG)),
      read_u32_le(&file[HEADER_CRC_OFFSET]));
  TEST_ASSERT_EQUAL_MEMORY(SMALL_CONFIG, &file[HEADER_LEN],
                           strlen(SMALL_CONFIG));
}

/* AC: specs/04-config-storage.md #1 */
void test_config_larger_than_4_kb_is_rejected_without_being_saved(void) {
  config_store_err_t err =
      config_store_save(g_oversized, NODE_CONFIG_MAX_RAW_LEN + 1);

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_TOO_LARGE, err);
  TEST_ASSERT_NULL(config_store_fs_mock_file(CONFIG_FILE, NULL));
}

/* AC: specs/04-config-storage.md #1 */
void test_empty_config_is_rejected_without_being_saved(void) {
  config_store_err_t err = config_store_save(SMALL_CONFIG, 0);

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_ARG, err);
  TEST_ASSERT_NULL(config_store_fs_mock_file(CONFIG_FILE, NULL));
}

/* AC: specs/04-config-storage.md #1 */
void test_config_is_not_saved_when_the_config_text_is_missing(void) {
  config_store_err_t err = config_store_save(NULL, strlen(SMALL_CONFIG));

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_ARG, err);
  TEST_ASSERT_NULL(config_store_fs_mock_file(CONFIG_FILE, NULL));
}

/* AC: specs/04-config-storage.md #1 */
void test_failed_flash_write_keeps_the_previous_config(void) {
  save_text(SMALL_CONFIG);
  config_store_fs_mock_set_error(CONFIG_STORE_FS_MOCK_OP_WRITE,
                                 CONFIG_STORE_ERR_IO);

  config_store_err_t err =
      config_store_save(OTHER_CONFIG, strlen(OTHER_CONFIG));

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_IO, err);
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, load());
  TEST_ASSERT_EQUAL_MEMORY(SMALL_CONFIG, g_loaded, strlen(SMALL_CONFIG));
}

/* ---- Detecting a bad stored config ---- */

/* AC: specs/04-config-storage.md #1 */
void test_loading_reports_that_no_config_is_stored_when_flash_is_empty(void) {
  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_NOT_FOUND, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_stored_config_with_one_corrupted_byte_is_reported_as_corrupt(void) {
  save_text(SMALL_CONFIG);
  /* Flip one bit of the first JSON byte, as a worn flash cell might. */
  stored_file(NULL)[HEADER_LEN] ^= 0x01u;

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_CORRUPT, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_stored_config_with_a_damaged_header_is_reported_as_corrupt(void) {
  save_text(SMALL_CONFIG);
  stored_file(NULL)[HEADER_MAGIC_OFFSET] ^= 0xFFu;

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_CORRUPT, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_stored_config_from_an_unknown_storage_format_version_is_reported_as_corrupt(
    void) {
  save_text(SMALL_CONFIG);
  write_u32_le(CONFIG_STORE_FORMAT_VERSION + 1u,
               &stored_file(NULL)[HEADER_VERSION_OFFSET]);

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_CORRUPT, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_stored_config_that_was_cut_short_is_reported_as_corrupt(void) {
  save_text(SMALL_CONFIG);
  size_t file_len = 0;
  uint8_t copy[HEADER_LEN + sizeof(SMALL_CONFIG)] = {0};
  (void)memcpy(copy, stored_file(&file_len), sizeof(copy));
  /* Store the same bytes minus the last one: the header still claims the
   * full length. */
  TEST_ASSERT_EQUAL_INT(
      0, config_store_fs_mock_put(CONFIG_FILE, copy, file_len - 1u));

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_CORRUPT, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_stored_file_shorter_than_the_header_is_reported_as_corrupt(void) {
  static const uint8_t partial_header[] = {'N', 'C', 'F', 'G'};
  TEST_ASSERT_EQUAL_INT(0, config_store_fs_mock_put(CONFIG_FILE, partial_header,
                                                    sizeof(partial_header)));

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_CORRUPT, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_stored_config_larger_than_the_reading_buffer_is_reported_as_too_large(
    void) {
  save_text(SMALL_CONFIG);
  char small_buf[4] = {0};
  size_t len = 0;

  config_store_err_t err =
      config_store_load(small_buf, sizeof(small_buf), &len);

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_TOO_LARGE, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_flash_read_failure_is_reported_as_a_storage_error(void) {
  save_text(SMALL_CONFIG);
  config_store_fs_mock_set_error(CONFIG_STORE_FS_MOCK_OP_READ,
                                 CONFIG_STORE_ERR_IO);

  config_store_err_t err = load();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_IO, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_loading_without_a_buffer_is_rejected(void) {
  save_text(SMALL_CONFIG);
  size_t len = 0;

  config_store_err_t err =
      config_store_load(NULL, NODE_CONFIG_MAX_RAW_LEN, &len);

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_ARG, err);
}

/* ---- Erasing ---- */

/* AC: specs/04-config-storage.md #1 */
void test_erased_config_is_no_longer_found(void) {
  save_text(SMALL_CONFIG);

  config_store_err_t err = config_store_erase();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, err);
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_NOT_FOUND, load());
}

/* AC: specs/04-config-storage.md #1 */
void test_erasing_succeeds_when_no_config_is_stored(void) {
  config_store_err_t err = config_store_erase();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, err);
}

/* AC: specs/04-config-storage.md #1 */
void test_erasing_also_removes_a_temporary_file_left_by_an_interrupted_save(
    void) {
  save_text(SMALL_CONFIG);
  TEST_ASSERT_EQUAL_INT(0, config_store_fs_mock_put(TMP_FILE, OTHER_CONFIG,
                                                    strlen(OTHER_CONFIG)));

  config_store_err_t err = config_store_erase();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, err);
  TEST_ASSERT_NULL(config_store_fs_mock_file(TMP_FILE, NULL));
  TEST_ASSERT_NULL(config_store_fs_mock_file(CONFIG_FILE, NULL));
}

/* AC: specs/04-config-storage.md #1 */
void test_flash_failure_during_erase_is_reported(void) {
  save_text(SMALL_CONFIG);
  config_store_fs_mock_set_error(CONFIG_STORE_FS_MOCK_OP_REMOVE,
                                 CONFIG_STORE_ERR_IO);

  config_store_err_t err = config_store_erase();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_IO, err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_config_that_was_saved_is_loaded_back_byte_for_byte);
  RUN_TEST(test_config_of_exactly_4_kb_is_saved_and_loaded_back);
  RUN_TEST(test_saving_a_new_config_replaces_the_previous_one);
  RUN_TEST(
      test_saved_config_is_stored_with_a_header_giving_its_length_and_checksum_in_little_endian_byte_order);
  RUN_TEST(test_config_larger_than_4_kb_is_rejected_without_being_saved);
  RUN_TEST(test_empty_config_is_rejected_without_being_saved);
  RUN_TEST(test_config_is_not_saved_when_the_config_text_is_missing);
  RUN_TEST(test_failed_flash_write_keeps_the_previous_config);
  RUN_TEST(test_loading_reports_that_no_config_is_stored_when_flash_is_empty);
  RUN_TEST(test_stored_config_with_one_corrupted_byte_is_reported_as_corrupt);
  RUN_TEST(test_stored_config_with_a_damaged_header_is_reported_as_corrupt);
  RUN_TEST(
      test_stored_config_from_an_unknown_storage_format_version_is_reported_as_corrupt);
  RUN_TEST(test_stored_config_that_was_cut_short_is_reported_as_corrupt);
  RUN_TEST(test_stored_file_shorter_than_the_header_is_reported_as_corrupt);
  RUN_TEST(
      test_stored_config_larger_than_the_reading_buffer_is_reported_as_too_large);
  RUN_TEST(test_flash_read_failure_is_reported_as_a_storage_error);
  RUN_TEST(test_loading_without_a_buffer_is_rejected);
  RUN_TEST(test_erased_config_is_no_longer_found);
  RUN_TEST(test_erasing_succeeds_when_no_config_is_stored);
  RUN_TEST(
      test_erasing_also_removes_a_temporary_file_left_by_an_interrupted_save);
  RUN_TEST(test_flash_failure_during_erase_is_reported);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void save_text(const char *json) {
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, config_store_save(json, strlen(json)));
}

static config_store_err_t load(void) {
  return config_store_load(g_loaded, sizeof(g_loaded), &g_loaded_len);
}

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_u32_le(uint32_t value, uint8_t *bytes) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static uint8_t *stored_file(size_t *out_len) {
  uint8_t *file = config_store_fs_mock_file(CONFIG_FILE, out_len);
  TEST_ASSERT_NOT_NULL_MESSAGE(file, "no config file is stored");
  return file;
}
