#pragma once

/**
 * @file node_dev_log_mock.h
 * @brief Test controls for the host implementation of node_dev_log.c.
 *
 * The mock records the devices AO's log lines in static memory instead of
 * logging them. Call node_dev_log_mock_reset() from every test's setUp() so
 * tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>

#include "node_config.h"
#include "node_dev.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Size of the recorded warning text, including the terminating NUL;
 *         longer warnings are truncated. */
#define NODE_DEV_LOG_MOCK_WARNING_LEN 128

/** @brief Size of the recorded info text, including the terminating NUL;
 *         longer lines are truncated. */
#define NODE_DEV_LOG_MOCK_INFO_LEN 128

/** @brief Most published events the mock keeps; later ones are counted but
 *         not stored. */
#define NODE_DEV_LOG_MOCK_MAX_EVENTS 32

/** @brief Most table rows, and state changes, the mock keeps; later ones are
 *         counted but not stored. */
#define NODE_DEV_LOG_MOCK_MAX_LINES 16

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief A recorded table row or state change. */
typedef struct {
  int dev_index; /**< Device's position in node_config_t::dev. */
  bool fault;    /**< State logged: true for FAULT. */
} node_dev_log_mock_line_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded log line. */
void node_dev_log_mock_reset(void);

/**
 * @brief Number of device table titles logged since the last reset.
 *
 * @return The count.
 */
size_t node_dev_log_mock_table_count(void);

/**
 * @brief Number of device table rows logged since the last reset.
 *
 * @return The count, including rows that were not stored.
 */
size_t node_dev_log_mock_row_count(void);

/**
 * @brief A recorded device table row.
 *
 * @param index Row number, 0 for the first since the last reset.
 * @return The row, or NULL if @p index was not stored.
 */
const node_dev_log_mock_line_t *node_dev_log_mock_row(size_t index);

/**
 * @brief Number of state changes logged since the last reset.
 *
 * @return The count, including changes that were not stored.
 */
size_t node_dev_log_mock_state_change_count(void);

/**
 * @brief A recorded state change.
 *
 * @param index Change number, 0 for the first since the last reset.
 * @return The change, or NULL if @p index was not stored.
 */
const node_dev_log_mock_line_t *node_dev_log_mock_state_change(size_t index);

/**
 * @brief Number of published events logged since the last reset.
 *
 * @return The count, including events that were not stored.
 */
size_t node_dev_log_mock_event_count(void);

/**
 * @brief A recorded published event.
 *
 * @param index Event number, 0 for the first since the last reset.
 * @return The event, or NULL if @p index was not stored.
 */
const node_dev_evt_t *node_dev_log_mock_event(size_t index);

/**
 * @brief Number of warnings logged since the last reset.
 *
 * @return The count.
 */
size_t node_dev_log_mock_warning_count(void);

/**
 * @brief Number of info lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_dev_log_mock_info_count(void);

/**
 * @brief Text of the last info line.
 *
 * @return A NUL-terminated string; empty if none was logged.
 */
const char *node_dev_log_mock_last_info(void);

/**
 * @brief Text of the last warning.
 *
 * @return The NUL-terminated text, or "" if none was logged since the last
 *         reset.
 */
const char *node_dev_log_mock_last_warning(void);
