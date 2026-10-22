#pragma once

/**
 * @file config_store_fs_mock.h
 * @brief Test controls for the host implementation of config_store_fs.c.
 *
 * The fake keeps files in static memory instead of flash. Tests can plant
 * files, change their bytes in place to simulate corruption, and make any
 * operation fail. Call config_store_fs_mock_reset() from every test's setUp()
 * so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "config_store.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Maximum number of files the fake holds at once. */
#define CONFIG_STORE_FS_MOCK_MAX_FILES 4

/** @brief Size of a file name buffer, including the terminating NUL. */
#define CONFIG_STORE_FS_MOCK_NAME_LEN 16

/** @brief Largest file the fake holds, in bytes; twice the config limit so
 *         tests can plant oversized files. */
#define CONFIG_STORE_FS_MOCK_MAX_FILE_LEN 8192

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief File operation whose result a test can force. */
typedef enum {
  CONFIG_STORE_FS_MOCK_OP_INIT,   /**< config_store_fs_init(). */
  CONFIG_STORE_FS_MOCK_OP_SIZE,   /**< config_store_fs_size(). */
  CONFIG_STORE_FS_MOCK_OP_READ,   /**< config_store_fs_read(). */
  CONFIG_STORE_FS_MOCK_OP_WRITE,  /**< config_store_fs_write_atomic(). */
  CONFIG_STORE_FS_MOCK_OP_REMOVE, /**< config_store_fs_remove(). */
  CONFIG_STORE_FS_MOCK_OP_COUNT,  /**< Number of operations; not an
                                     operation. */
} config_store_fs_mock_op_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Delete every file and clear every forced error. */
void config_store_fs_mock_reset(void);

/**
 * @brief Make every later call of @p op fail, until the next reset.
 *
 * A failing write or remove leaves the files unchanged, as an atomic write
 * interrupted by a power cut would.
 *
 * @param op  Operation to fail; out-of-range values are ignored.
 * @param err Error the operation returns; CONFIG_STORE_OK stops forcing.
 */
void config_store_fs_mock_set_error(config_store_fs_mock_op_t op,
                                    config_store_err_t err);

/**
 * @brief Create or replace a file directly, bypassing forced errors.
 *
 * @param name NUL-terminated file name, shorter than
 *             CONFIG_STORE_FS_MOCK_NAME_LEN.
 * @param data Contents; may be NULL only if @p len is 0.
 * @param len  Number of bytes, at most CONFIG_STORE_FS_MOCK_MAX_FILE_LEN.
 * @return 0 on success, -1 for a bad argument or no free file slot.
 */
int config_store_fs_mock_put(const char *name, const void *data, size_t len);

/**
 * @brief Contents of a file, writable so a test can corrupt bytes in place.
 *
 * The pointer stays valid until the file is replaced or removed, or the fake
 * is reset.
 *
 * @param name    File name.
 * @param out_len Receives the file size; may be NULL.
 * @return The contents, or NULL if the file does not exist.
 */
uint8_t *config_store_fs_mock_file(const char *name, size_t *out_len);

/**
 * @brief Number of calls to config_store_fs_init() since the last reset.
 *
 * @return The count, including calls that were forced to fail.
 */
size_t config_store_fs_mock_init_count(void);
