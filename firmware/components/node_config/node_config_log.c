/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"

#include "node_config_internal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_config component. */
#define NODE_CONFIG_LOG_TAG "node_config"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_config_log_warning(const char *path, const char *fmt, ...) {
  char reason[NODE_CONFIG_ERR_MSG_LEN] = {0};
  if (fmt != NULL) {
    va_list args;
    va_start(args, fmt);
    /* A reason longer than the buffer is truncated on purpose. */
    (void)vsnprintf(reason, sizeof(reason), fmt, args);
    va_end(args);
  }
  ESP_LOGW(NODE_CONFIG_LOG_TAG, "%s: %s", (path != NULL) ? path : "", reason);
}
