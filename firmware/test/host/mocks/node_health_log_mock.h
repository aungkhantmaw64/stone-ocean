#pragma once

/**
 * @file node_health_log_mock.h
 * @brief Test controls for the host implementation of node_health_log.c.
 *
 * The mock records node_health's log lines instead of logging them. Call
 * node_health_log_mock_reset() from every test's setUp() so tests stay
 * independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded log line. */
void node_health_log_mock_reset(void);

/**
 * @brief Number of "erasing the nvs partition" lines logged since the last
 *        reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_nvs_erasing_count(void);

/**
 * @brief Number of "nvs init failed" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_nvs_failed_count(void);

/**
 * @brief Number of crash counter failures logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_counter_failed_count(void);

/**
 * @brief Number of boot lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_boot_count(void);

/**
 * @brief Reset reason of the last boot line logged.
 *
 * @return A NUL-terminated copy; "" if none was logged.
 */
const char *node_health_log_mock_last_boot_reason(void);

/**
 * @brief Crash count of the last boot line logged.
 *
 * @return The count, or 0 if none was logged.
 */
uint32_t node_health_log_mock_last_boot_crash_count(void);

/**
 * @brief Number of `FACTORY RESET` lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_factory_reset_count(void);

/**
 * @brief Number of failed factory reset steps logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_factory_reset_failed_count(void);

/**
 * @brief Number of "health AO did not start" lines logged since the last
 *        reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_ao_start_failed_count(void);

/**
 * @brief Number of "reset button watched" lines logged since the last
 *        reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_button_started_count(void);

/**
 * @brief Number of button pin setup failures logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_button_setup_failed_count(void);

/**
 * @brief Number of "reset button held" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_button_held_count(void);

/**
 * @brief Number of "released early" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_health_log_mock_button_released_early_count(void);
