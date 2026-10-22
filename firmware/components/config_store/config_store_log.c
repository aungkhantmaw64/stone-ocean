/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "config_store_log.h"

#include <stddef.h>

#include "esp_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the config_store component. */
#define CONFIG_STORE_LOG_TAG "config_store"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void config_store_log_safe_mode(const char *reason) {
  ESP_LOGE(CONFIG_STORE_LOG_TAG, "SAFE MODE: %s",
           (reason != NULL) ? reason : "");
}

void config_store_log_normal_mode(const char *node_name, int device_count) {
  ESP_LOGI(CONFIG_STORE_LOG_TAG, "normal mode: node '%s', %d device(s)",
           (node_name != NULL) ? node_name : "", device_count);
}
