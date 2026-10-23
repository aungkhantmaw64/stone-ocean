#pragma once

/**
 * @file node_health_log.h
 * @brief Log lines of node_health. Not part of the public interface.
 *
 * Logs with ESP_LOG under the tag "node_health" on the target. The host
 * mock records what was logged so tests can check it.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Log at ERROR level that NVS cannot be used as it is, so the `nvs`
 *        partition is erased.
 *
 * @param why NUL-terminated reason, such as "no free pages".
 */
void node_health_log_nvs_erasing(const char *why);

/**
 * @brief Log at ERROR level that NVS could not be initialised, so the crash
 *        counter is not kept this boot.
 */
void node_health_log_nvs_failed(void);

/**
 * @brief Log at WARNING level that the crash counter could not be read or
 *        written.
 *
 * @param action NUL-terminated "read", "write" or "clear".
 */
void node_health_log_counter_failed(const char *action);

/**
 * @brief Log at INFO level the boot line `reset_reason=<reason>
 *        crash_count=<n>`.
 *
 * @param reason      NUL-terminated reset reason.
 * @param crash_count Crash count, this boot's reset included.
 */
void node_health_log_boot(const char *reason, uint32_t crash_count);

/**
 * @brief Log at WARNING level `FACTORY RESET`, as a factory reset starts.
 */
void node_health_log_factory_reset(void);

/**
 * @brief Log at ERROR level that a factory reset step failed; the reset
 *        goes on.
 *
 * @param what NUL-terminated name of the step, such as "config erase".
 */
void node_health_log_factory_reset_failed(const char *what);
