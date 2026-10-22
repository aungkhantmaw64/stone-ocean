#pragma once

/**
 * @file config_store.h
 * @brief Flash persistence of the raw JSON node config.
 *
 * The raw JSON is stored exactly as uploaded, in one file on the dedicated
 * `config` LittleFS partition, guarded by a header with a CRC32. The parsed
 * struct is never stored. The header must not include ESP-IDF headers.
 *
 * Functions are called from task context, never from an ISR, and are not
 * safe to call from two tasks at once.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Storage format version written to the file header. Bump it when
 *         the header layout changes. */
#define CONFIG_STORE_FORMAT_VERSION 1

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a config_store call. */
typedef enum {
  CONFIG_STORE_OK = 0,        /**< Success. */
  CONFIG_STORE_ERR_ARG,       /**< NULL pointer or bad argument from the
                                 caller. */
  CONFIG_STORE_ERR_NOT_FOUND, /**< No config is stored. */
  CONFIG_STORE_ERR_CORRUPT,   /**< The stored config has a bad header or a
                                 CRC mismatch. */
  CONFIG_STORE_ERR_TOO_LARGE, /**< The input, or the stored config, does not
                                 fit the limit or the caller's buffer. */
  CONFIG_STORE_ERR_IO,        /**< Flash or filesystem failure. */
} config_store_err_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Store a raw JSON config in flash, replacing any stored config.
 *
 * The caller must already have validated the JSON with node_config_parse();
 * only the arguments and the size limit are checked here. The save is
 * atomic: after a power cut, either the old or the new config is stored. The
 * new config takes effect at the next reboot.
 *
 * @param json Raw JSON text; need not be NUL-terminated. Must not be NULL.
 * @param len  Number of bytes in @p json; 1 to NODE_CONFIG_MAX_RAW_LEN.
 * @return CONFIG_STORE_OK, CONFIG_STORE_ERR_ARG for a NULL @p json or a zero
 *         @p len, CONFIG_STORE_ERR_TOO_LARGE if @p len exceeds
 *         NODE_CONFIG_MAX_RAW_LEN (nothing is written), or
 *         CONFIG_STORE_ERR_IO.
 */
config_store_err_t config_store_save(const char *json, size_t len);

/**
 * @brief Read the stored raw JSON config and verify its CRC.
 *
 * @param buf     Receives the JSON, not NUL-terminated. Must not be NULL.
 *                Unspecified on failure.
 * @param buf_len Size of @p buf in bytes; NODE_CONFIG_MAX_RAW_LEN always
 *                fits.
 * @param out_len Receives the number of JSON bytes on success. Must not be
 *                NULL.
 * @return CONFIG_STORE_OK, CONFIG_STORE_ERR_ARG for a NULL pointer,
 *         CONFIG_STORE_ERR_NOT_FOUND if nothing is stored,
 *         CONFIG_STORE_ERR_CORRUPT for a bad header or CRC mismatch,
 *         CONFIG_STORE_ERR_TOO_LARGE if the stored JSON does not fit @p buf,
 *         or CONFIG_STORE_ERR_IO.
 */
config_store_err_t config_store_load(char *buf, size_t buf_len,
                                     size_t *out_len);

/**
 * @brief Remove the stored config, for example during factory reset.
 *
 * @return CONFIG_STORE_OK, also when nothing was stored, or
 *         CONFIG_STORE_ERR_IO.
 */
config_store_err_t config_store_erase(void);

#ifdef __cplusplus
}
#endif
