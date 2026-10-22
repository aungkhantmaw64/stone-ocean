/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "node_dev_log.h"
#include "node_dev_log_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Device table titles logged since the last reset. */
static size_t g_table_count = 0;

/** @brief Device table rows logged since the last reset. */
static size_t g_row_count = 0;

/** @brief The first NODE_DEV_LOG_MOCK_MAX_LINES rows, in logged order. */
static node_dev_log_mock_line_t g_rows[NODE_DEV_LOG_MOCK_MAX_LINES] = {0};

/** @brief State changes logged since the last reset. */
static size_t g_state_change_count = 0;

/** @brief The first NODE_DEV_LOG_MOCK_MAX_LINES state changes, in logged
 *         order. */
static node_dev_log_mock_line_t g_state_changes[NODE_DEV_LOG_MOCK_MAX_LINES] = {
    0};

/** @brief Published events logged since the last reset. */
static size_t g_event_count = 0;

/** @brief The first NODE_DEV_LOG_MOCK_MAX_EVENTS events, in logged order. */
static node_dev_evt_t g_events[NODE_DEV_LOG_MOCK_MAX_EVENTS] = {0};

/** @brief Warnings logged since the last reset. */
static size_t g_warning_count = 0;

/** @brief Text of the last warning, NUL-terminated. */
static char g_last_warning[NODE_DEV_LOG_MOCK_WARNING_LEN] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Store a line if there is room, and count it.
 *
 * @param lines     Storage of NODE_DEV_LOG_MOCK_MAX_LINES lines.
 * @param count     Lines counted so far; incremented.
 * @param dev_index Device index to record.
 * @param fault     State to record.
 */
static void node_dev_log_mock_record_line(node_dev_log_mock_line_t *lines,
                                          size_t *count, int dev_index,
                                          bool fault);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_dev_log_mock_reset(void) {
  g_table_count = 0;
  g_row_count = 0;
  (void)memset(g_rows, 0, sizeof(g_rows));
  g_state_change_count = 0;
  (void)memset(g_state_changes, 0, sizeof(g_state_changes));
  g_event_count = 0;
  (void)memset(g_events, 0, sizeof(g_events));
  g_warning_count = 0;
  (void)memset(g_last_warning, 0, sizeof(g_last_warning));
}

size_t node_dev_log_mock_table_count(void) { return g_table_count; }

size_t node_dev_log_mock_row_count(void) { return g_row_count; }

const node_dev_log_mock_line_t *node_dev_log_mock_row(size_t index) {
  if (index >= g_row_count || index >= NODE_DEV_LOG_MOCK_MAX_LINES) {
    return NULL;
  }
  return &g_rows[index];
}

size_t node_dev_log_mock_state_change_count(void) {
  return g_state_change_count;
}

const node_dev_log_mock_line_t *node_dev_log_mock_state_change(size_t index) {
  if (index >= g_state_change_count || index >= NODE_DEV_LOG_MOCK_MAX_LINES) {
    return NULL;
  }
  return &g_state_changes[index];
}

size_t node_dev_log_mock_event_count(void) { return g_event_count; }

const node_dev_evt_t *node_dev_log_mock_event(size_t index) {
  if (index >= g_event_count || index >= NODE_DEV_LOG_MOCK_MAX_EVENTS) {
    return NULL;
  }
  return &g_events[index];
}

size_t node_dev_log_mock_warning_count(void) { return g_warning_count; }

const char *node_dev_log_mock_last_warning(void) { return g_last_warning; }

void node_dev_log_table_title(int n_dev) {
  (void)n_dev;
  if (g_table_count < SIZE_MAX) {
    g_table_count++;
  }
}

void node_dev_log_table_row(int dev_index, const node_config_dev_t *dev,
                            bool fault) {
  if (dev == NULL) {
    return;
  }
  node_dev_log_mock_record_line(g_rows, &g_row_count, dev_index, fault);
}

void node_dev_log_state_change(int dev_index, const char *id, bool fault) {
  (void)id;
  node_dev_log_mock_record_line(g_state_changes, &g_state_change_count,
                                dev_index, fault);
}

void node_dev_log_event(const char *id, const node_dev_evt_t *evt) {
  (void)id;
  if (evt == NULL) {
    return;
  }
  if (g_event_count < NODE_DEV_LOG_MOCK_MAX_EVENTS) {
    g_events[g_event_count] = *evt;
  }
  if (g_event_count < SIZE_MAX) {
    g_event_count++;
  }
}

void node_dev_log_warning(const char *fmt, ...) {
  if (fmt == NULL) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  /* Truncation to the buffer is deliberate; tests check short prefixes. */
  (void)vsnprintf(g_last_warning, sizeof(g_last_warning), fmt, args);
  va_end(args);
  if (g_warning_count < SIZE_MAX) {
    g_warning_count++;
  }
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_dev_log_mock_record_line(node_dev_log_mock_line_t *lines,
                                          size_t *count, int dev_index,
                                          bool fault) {
  if (*count < NODE_DEV_LOG_MOCK_MAX_LINES) {
    lines[*count].dev_index = dev_index;
    lines[*count].fault = fault;
  }
  if (*count < SIZE_MAX) {
    (*count)++;
  }
}
