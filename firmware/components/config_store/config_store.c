/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "config_store.h"

#include <stdint.h>

#include "config_store_crc.h"
#include "config_store_fs.h"
#include "node_config.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief File that holds the header and the raw JSON. */
#define CONFIG_STORE_FILE_NAME "config.bin"

/** @brief File written first during a save, then renamed over
 *         CONFIG_STORE_FILE_NAME. */
#define CONFIG_STORE_TMP_FILE_NAME "config.tmp"

/** @brief First header field: "NCFG" read as a little-endian uint32_t. */
#define CONFIG_STORE_MAGIC 0x4746434EU

/** @brief Number of uint32_t fields in the header. */
#define CONFIG_STORE_HEADER_FIELDS 4U

/** @brief Size of the file header in bytes. */
#define CONFIG_STORE_HEADER_LEN (CONFIG_STORE_HEADER_FIELDS * sizeof(uint32_t))

/** @brief Number of bits in a byte, for little-endian packing. */
#define CONFIG_STORE_BITS_PER_BYTE 8U

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Decoded file header. Stored as four little-endian uint32_t, in
 *         this order, independent of struct layout. */
typedef struct {
  uint32_t magic;    /**< CONFIG_STORE_MAGIC in a valid file. */
  uint32_t version;  /**< Storage format version; CONFIG_STORE_FORMAT_VERSION
                        in a file this firmware can read. */
  uint32_t json_len; /**< Number of JSON bytes after the header, 1 to
                        NODE_CONFIG_MAX_RAW_LEN. */
  uint32_t crc;      /**< CRC32 of the JSON bytes. */
} config_store_header_t;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Pack a header into its on-flash byte layout.
 *
 * @param header Header to pack; must not be NULL.
 * @param out    Receives CONFIG_STORE_HEADER_LEN bytes; must not be NULL.
 */
static void config_store_header_encode(const config_store_header_t *header,
                                       uint8_t out[CONFIG_STORE_HEADER_LEN]);

/**
 * @brief Unpack a header from its on-flash byte layout.
 *
 * @param in     CONFIG_STORE_HEADER_LEN bytes read from flash; must not be
 *               NULL.
 * @param header Receives the fields; must not be NULL.
 */
static void config_store_header_decode(
    const uint8_t in[CONFIG_STORE_HEADER_LEN], config_store_header_t *header);

/**
 * @brief Write a uint32_t as 4 little-endian bytes.
 *
 * @param value Value to write.
 * @param out   Receives 4 bytes; must not be NULL.
 */
static void config_store_put_u32_le(uint32_t value, uint8_t *out);

/**
 * @brief Read a uint32_t from 4 little-endian bytes.
 *
 * @param in 4 bytes; must not be NULL.
 * @return The value.
 */
static uint32_t config_store_get_u32_le(const uint8_t *in);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

config_store_err_t config_store_save(const char *json, size_t len) {
  if (json == NULL || len == 0) {
    return CONFIG_STORE_ERR_ARG;
  }
  if (len > NODE_CONFIG_MAX_RAW_LEN) {
    return CONFIG_STORE_ERR_TOO_LARGE;
  }

  config_store_header_t header = {
      .magic = CONFIG_STORE_MAGIC,
      .version = CONFIG_STORE_FORMAT_VERSION,
      .json_len = (uint32_t)len,
      .crc = config_store_crc_compute(json, len),
  };
  uint8_t header_bytes[CONFIG_STORE_HEADER_LEN] = {0};
  config_store_header_encode(&header, header_bytes);

  return config_store_fs_write_atomic(CONFIG_STORE_FILE_NAME,
                                      CONFIG_STORE_TMP_FILE_NAME, header_bytes,
                                      sizeof(header_bytes), json, len);
}

config_store_err_t config_store_load(char *buf, size_t buf_len,
                                     size_t *out_len) {
  if (buf == NULL || out_len == NULL) {
    return CONFIG_STORE_ERR_ARG;
  }

  size_t file_size = 0;
  config_store_err_t err =
      config_store_fs_size(CONFIG_STORE_FILE_NAME, &file_size);
  if (err != CONFIG_STORE_OK) {
    return err;
  }
  if (file_size < CONFIG_STORE_HEADER_LEN) {
    return CONFIG_STORE_ERR_CORRUPT;
  }

  uint8_t header_bytes[CONFIG_STORE_HEADER_LEN] = {0};
  err = config_store_fs_read(CONFIG_STORE_FILE_NAME, 0, header_bytes,
                             sizeof(header_bytes));
  if (err != CONFIG_STORE_OK) {
    return err;
  }
  config_store_header_t header = {0};
  config_store_header_decode(header_bytes, &header);

  size_t json_len = file_size - CONFIG_STORE_HEADER_LEN;
  if (header.magic != CONFIG_STORE_MAGIC ||
      header.version != CONFIG_STORE_FORMAT_VERSION ||
      (size_t)header.json_len != json_len || json_len == 0) {
    return CONFIG_STORE_ERR_CORRUPT;
  }
  if (json_len > NODE_CONFIG_MAX_RAW_LEN || json_len > buf_len) {
    return CONFIG_STORE_ERR_TOO_LARGE;
  }

  err = config_store_fs_read(CONFIG_STORE_FILE_NAME, CONFIG_STORE_HEADER_LEN,
                             buf, json_len);
  if (err != CONFIG_STORE_OK) {
    return err;
  }
  if (config_store_crc_compute(buf, json_len) != header.crc) {
    return CONFIG_STORE_ERR_CORRUPT;
  }

  *out_len = json_len;
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_erase(void) {
  /* A temporary file left by an interrupted save is removed too, so a
   * factory reset leaves no trace of the old config. */
  config_store_err_t tmp_err =
      config_store_fs_remove(CONFIG_STORE_TMP_FILE_NAME);
  config_store_err_t err = config_store_fs_remove(CONFIG_STORE_FILE_NAME);
  if (err != CONFIG_STORE_OK) {
    return err;
  }
  return tmp_err;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void config_store_header_encode(const config_store_header_t *header,
                                       uint8_t out[CONFIG_STORE_HEADER_LEN]) {
  config_store_put_u32_le(header->magic, &out[0 * sizeof(uint32_t)]);
  config_store_put_u32_le(header->version, &out[1 * sizeof(uint32_t)]);
  config_store_put_u32_le(header->json_len, &out[2 * sizeof(uint32_t)]);
  config_store_put_u32_le(header->crc, &out[3 * sizeof(uint32_t)]);
}

static void config_store_header_decode(
    const uint8_t in[CONFIG_STORE_HEADER_LEN], config_store_header_t *header) {
  header->magic = config_store_get_u32_le(&in[0 * sizeof(uint32_t)]);
  header->version = config_store_get_u32_le(&in[1 * sizeof(uint32_t)]);
  header->json_len = config_store_get_u32_le(&in[2 * sizeof(uint32_t)]);
  header->crc = config_store_get_u32_le(&in[3 * sizeof(uint32_t)]);
}

static void config_store_put_u32_le(uint32_t value, uint8_t *out) {
  for (size_t i = 0; i < sizeof(uint32_t); i++) {
    out[i] = (uint8_t)(value >> (CONFIG_STORE_BITS_PER_BYTE * i));
  }
}

static uint32_t config_store_get_u32_le(const uint8_t *in) {
  uint32_t value = 0;
  for (size_t i = 0; i < sizeof(uint32_t); i++) {
    value |= (uint32_t)in[i] << (CONFIG_STORE_BITS_PER_BYTE * i);
  }
  return value;
}
