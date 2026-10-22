/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_store_log.h"
#include "config_store_log_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief "SAFE MODE" lines logged since the last reset. */
static size_t g_safe_mode_count = 0;

/** @brief Reason of the last "SAFE MODE" line, NUL-terminated; written by
 *         config_store_log_safe_mode(). */
static char g_safe_mode_reason[CONFIG_STORE_LOG_MOCK_REASON_LEN] = {0};

/** @brief Normal-mode lines logged since the last reset. */
static size_t g_normal_mode_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void config_store_log_mock_reset(void) {
  g_safe_mode_count = 0;
  (void)memset(g_safe_mode_reason, 0, sizeof(g_safe_mode_reason));
  g_normal_mode_count = 0;
}

size_t config_store_log_mock_safe_mode_count(void) { return g_safe_mode_count; }

const char *config_store_log_mock_safe_mode_reason(void) {
  return g_safe_mode_reason;
}

size_t config_store_log_mock_normal_mode_count(void) {
  return g_normal_mode_count;
}

void config_store_log_safe_mode(const char *reason) {
  /* Truncation to the buffer is deliberate; tests use short reasons. */
  (void)snprintf(g_safe_mode_reason, sizeof(g_safe_mode_reason), "%s",
                 (reason != NULL) ? reason : "");
  if (g_safe_mode_count < SIZE_MAX) {
    g_safe_mode_count++;
  }
}

void config_store_log_normal_mode(const char *node_name, int device_count) {
  (void)node_name;
  (void)device_count;
  if (g_normal_mode_count < SIZE_MAX) {
    g_normal_mode_count++;
  }
}
