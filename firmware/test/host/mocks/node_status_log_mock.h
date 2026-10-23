#pragma once

/**
 * @file node_status_log_mock.h
 * @brief Test controls for the host implementation of node_status_log.c.
 *
 * The mock counts the status AO's log lines instead of logging them. Call
 * node_status_log_mock_reset() from every test's setUp() so tests stay
 * independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded log line. */
void node_status_log_mock_reset(void);

/**
 * @brief Number of "started" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_status_log_mock_started_count(void);

/**
 * @brief Number of pin setup failures logged since the last reset.
 *
 * @return The count.
 */
size_t node_status_log_mock_setup_failed_count(void);

/**
 * @brief Number of steady on or off mode changes logged since the last
 *        reset.
 *
 * @return The count.
 */
size_t node_status_log_mock_steady_count(void);

/**
 * @brief The last steady mode change logged.
 *
 * @return True for on; false for off or if none was logged.
 */
bool node_status_log_mock_last_steady_on(void);

/**
 * @brief Number of "writes start failing" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_status_log_mock_write_failed_count(void);

/**
 * @brief Number of "writes work again" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_status_log_mock_write_recovered_count(void);
