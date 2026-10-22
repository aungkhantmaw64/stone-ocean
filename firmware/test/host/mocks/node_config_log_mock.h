#pragma once

/**
 * @file node_config_log_mock.h
 * @brief Test controls for the host implementation of node_config_log.c.
 *
 * The mock records warnings in static memory instead of logging them. Call
 * node_config_log_mock_reset() from every test's setUp() so tests stay
 * independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Maximum number of warnings the mock keeps; later ones are counted
 *         but not stored. */
#define NODE_CONFIG_LOG_MOCK_MAX_WARNINGS 16

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded warning. */
void node_config_log_mock_reset(void);

/**
 * @brief Number of warnings logged since the last reset.
 *
 * @return The count, including warnings beyond
 *         NODE_CONFIG_LOG_MOCK_MAX_WARNINGS that were not stored.
 */
size_t node_config_log_mock_count(void);

/**
 * @brief JSON path of a recorded warning.
 *
 * @param index Warning number, 0 for the first since the last reset.
 * @return The NUL-terminated path, or NULL if @p index was not stored.
 */
const char *node_config_log_mock_path(size_t index);

/**
 * @brief Reason of a recorded warning, without the path.
 *
 * @param index Warning number, 0 for the first since the last reset.
 * @return The NUL-terminated reason, or NULL if @p index was not stored.
 */
const char *node_config_log_mock_reason(size_t index);
