#pragma once

/**
 * @file config_store_log.h
 * @brief Boot-mode log lines of config_store. Not part of the public
 *        interface.
 *
 * Logs with ESP_LOG on the target. The host mock records the lines so tests
 * can check them.
 */

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Log "SAFE MODE: <reason>" at ERROR level.
 *
 * @param reason NUL-terminated reason; NULL is logged as "".
 */
void config_store_log_safe_mode(const char *reason);

/**
 * @brief Log at INFO level that the node runs in normal mode.
 *
 * @param node_name    NUL-terminated node name from the config; NULL is
 *                     logged as "".
 * @param device_count Number of configured devices.
 */
void config_store_log_normal_mode(const char *node_name, int device_count);
