/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "node_config_internal.h"
#include "node_config_log_mock.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One recorded warning. */
typedef struct {
  char path[NODE_CONFIG_ERR_PATH_LEN];  /**< JSON path, NUL-terminated and
                                           truncated to fit. */
  char reason[NODE_CONFIG_ERR_MSG_LEN]; /**< Reason, NUL-terminated and
                                           truncated to fit. */
} node_config_log_mock_entry_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Stored warnings, oldest first; written by node_config_log_warning().
 */
static node_config_log_mock_entry_t
    g_warnings[NODE_CONFIG_LOG_MOCK_MAX_WARNINGS] = {0};

/** @brief Warnings logged since the last reset, stored or not. */
static size_t g_warning_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_config_log_mock_reset(void) {
  (void)memset(g_warnings, 0, sizeof(g_warnings));
  g_warning_count = 0;
}

size_t node_config_log_mock_count(void) { return g_warning_count; }

const char *node_config_log_mock_path(size_t index) {
  if (index >= g_warning_count || index >= NODE_CONFIG_LOG_MOCK_MAX_WARNINGS) {
    return NULL;
  }
  return g_warnings[index].path;
}

const char *node_config_log_mock_reason(size_t index) {
  if (index >= g_warning_count || index >= NODE_CONFIG_LOG_MOCK_MAX_WARNINGS) {
    return NULL;
  }
  return g_warnings[index].reason;
}

void node_config_log_warning(const char *path, const char *fmt, ...) {
  if (g_warning_count < NODE_CONFIG_LOG_MOCK_MAX_WARNINGS) {
    node_config_log_mock_entry_t *entry = &g_warnings[g_warning_count];
    /* Truncation to the entry buffers is deliberate, as on the target. */
    (void)snprintf(entry->path, sizeof(entry->path), "%s",
                   (path != NULL) ? path : "");
    if (fmt != NULL) {
      va_list args;
      va_start(args, fmt);
      (void)vsnprintf(entry->reason, sizeof(entry->reason), fmt, args);
      va_end(args);
    }
  }
  if (g_warning_count < SIZE_MAX) {
    g_warning_count++;
  }
}
