#pragma once

/**
 * @file config_store_log_mock.h
 * @brief Test controls for the host implementation of config_store_log.c.
 *
 * The mock records the boot-mode log lines in static memory instead of
 * logging them. Call config_store_log_mock_reset() from every test's setUp()
 * so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Size of the recorded reason buffer, including the terminating NUL;
 *         longer reasons are truncated. */
#define CONFIG_STORE_LOG_MOCK_REASON_LEN 128

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded log line. */
void config_store_log_mock_reset(void);

/**
 * @brief Number of "SAFE MODE" lines logged since the last reset.
 *
 * @return The count.
 */
size_t config_store_log_mock_safe_mode_count(void);

/**
 * @brief Reason of the last "SAFE MODE" line.
 *
 * @return The NUL-terminated reason, or "" if none was logged since the last
 *         reset.
 */
const char *config_store_log_mock_safe_mode_reason(void);

/**
 * @brief Number of normal-mode lines logged since the last reset.
 *
 * @return The count.
 */
size_t config_store_log_mock_normal_mode_count(void);
