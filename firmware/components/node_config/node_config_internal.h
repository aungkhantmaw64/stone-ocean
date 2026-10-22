#pragma once

/**
 * @file node_config_internal.h
 * @brief Helpers shared by the node_config sources. Not part of the public
 *        interface.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_config.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Reset @p err to "no error": code NODE_CONFIG_OK, empty path and msg.
 *
 * @param err Error to reset; NULL is ignored.
 */
void node_config_err_clear(node_config_err_t *err);

/**
 * @brief Record an error in @p err.
 *
 * The message becomes "<path>: <reason>", or just the reason when @p path is
 * empty. Path and message are truncated to fit their buffers.
 *
 * @param err  Error to fill; NULL is allowed and only the code is returned.
 * @param code Error code to record.
 * @param path JSON path of the bad value, for example "devices[1].gpio"; NULL
 *             or "" when the error concerns the whole input.
 * @param fmt  printf-style format of the reason; must not be NULL.
 * @return @p code, so a caller can write `return node_config_err_set(...)`.
 */
node_config_err_code_t
node_config_err_set(node_config_err_t *err, node_config_err_code_t code,
                    const char *path, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * @brief Log a warning about a config value that is accepted anyway.
 *
 * Logs "<path>: <reason>" with ESP_LOGW on the target. The host mock records
 * the path and reason so tests can check them. The reason is truncated to
 * NODE_CONFIG_ERR_MSG_LEN - 1 characters.
 *
 * @param path JSON path of the value; NULL is treated as "".
 * @param fmt  printf-style format of the reason; must not be NULL.
 */
void node_config_log_warning(const char *path, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
