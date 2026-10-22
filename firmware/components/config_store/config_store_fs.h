#pragma once

/**
 * @file config_store_fs.h
 * @brief File access used by config_store. Not part of the public interface.
 *
 * Implemented on LittleFS in config_store_fs.c on the target, and by an
 * in-memory fake with the same file name on the host. File names are plain
 * names inside the config partition, without a directory, for example
 * "config.bin". Called from task context only.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>

#include "config_store.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Mount the config partition, formatting it first if it does not
 *        mount.
 *
 * Call once at boot before any other config_store function.
 *
 * @return CONFIG_STORE_OK, or CONFIG_STORE_ERR_IO if the partition is missing
 *         or still does not mount after formatting.
 */
config_store_err_t config_store_fs_init(void);

/**
 * @brief Size of a file.
 *
 * @param name     File name; must not be NULL.
 * @param out_size Receives the size in bytes; must not be NULL.
 * @return CONFIG_STORE_OK, CONFIG_STORE_ERR_ARG for a NULL pointer,
 *         CONFIG_STORE_ERR_NOT_FOUND if the file does not exist, or
 *         CONFIG_STORE_ERR_IO.
 */
config_store_err_t config_store_fs_size(const char *name, size_t *out_size);

/**
 * @brief Read exactly @p len bytes of a file, starting at @p offset.
 *
 * @param name   File name; must not be NULL.
 * @param offset Byte offset of the first byte to read.
 * @param buf    Receives the bytes; must not be NULL unless @p len is 0.
 * @param len    Number of bytes to read.
 * @return CONFIG_STORE_OK, CONFIG_STORE_ERR_ARG for a NULL pointer,
 *         CONFIG_STORE_ERR_NOT_FOUND if the file does not exist,
 *         CONFIG_STORE_ERR_CORRUPT if the file ends before @p len bytes, or
 *         CONFIG_STORE_ERR_IO.
 */
config_store_err_t config_store_fs_read(const char *name, size_t offset,
                                        void *buf, size_t len);

/**
 * @brief Replace a file atomically with @p head followed by @p body.
 *
 * Writes everything to @p tmp_name, closes it, then renames it over
 * @p name. After a power cut, @p name holds either its old or its new
 * contents. On failure the temporary file is removed where possible.
 *
 * @param name     File to replace; must not be NULL.
 * @param tmp_name Temporary file name, different from @p name; must not be
 *                 NULL.
 * @param head     First part of the contents; must not be NULL unless
 *                 @p head_len is 0.
 * @param head_len Number of bytes in @p head.
 * @param body     Second part of the contents; must not be NULL unless
 *                 @p body_len is 0.
 * @param body_len Number of bytes in @p body.
 * @return CONFIG_STORE_OK, CONFIG_STORE_ERR_ARG for a NULL pointer, or
 *         CONFIG_STORE_ERR_IO.
 */
config_store_err_t
config_store_fs_write_atomic(const char *name, const char *tmp_name,
                             const void *head, size_t head_len,
                             const void *body, size_t body_len);

/**
 * @brief Delete a file.
 *
 * @param name File name; must not be NULL.
 * @return CONFIG_STORE_OK, also when the file does not exist,
 *         CONFIG_STORE_ERR_ARG for a NULL @p name, or CONFIG_STORE_ERR_IO.
 */
config_store_err_t config_store_fs_remove(const char *name);
