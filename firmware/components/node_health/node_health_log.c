/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_health_log.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_health component. */
#define NODE_HEALTH_LOG_TAG "node_health"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_health_log_nvs_erasing(const char *why) {
  ESP_LOGE(NODE_HEALTH_LOG_TAG, "nvs: %s; erasing the nvs partition",
           (why != NULL) ? why : "?");
}

void node_health_log_nvs_failed(void) {
  ESP_LOGE(NODE_HEALTH_LOG_TAG,
           "nvs: init failed; the crash counter is not kept this boot");
}

void node_health_log_counter_failed(const char *action) {
  ESP_LOGW(NODE_HEALTH_LOG_TAG, "crash counter: %s failed",
           (action != NULL) ? action : "?");
}

void node_health_log_boot(const char *reason, uint32_t crash_count) {
  ESP_LOGI(NODE_HEALTH_LOG_TAG, "reset_reason=%s crash_count=%lu",
           (reason != NULL) ? reason : "unknown", (unsigned long)crash_count);
}

void node_health_log_factory_reset(void) {
  ESP_LOGW(NODE_HEALTH_LOG_TAG, "FACTORY RESET");
}

void node_health_log_factory_reset_failed(const char *what) {
  ESP_LOGE(NODE_HEALTH_LOG_TAG, "factory reset: %s failed; going on",
           (what != NULL) ? what : "?");
}

void node_health_log_ao_start_failed(void) {
  ESP_LOGE(NODE_HEALTH_LOG_TAG,
           "health AO did not start; the reset button does nothing");
}

void node_health_log_button_started(int gpio) {
  ESP_LOGI(NODE_HEALTH_LOG_TAG, "reset button: watching GPIO %d", gpio);
}

void node_health_log_button_setup_failed(int gpio, node_hal_err_t err) {
  ESP_LOGW(NODE_HEALTH_LOG_TAG,
           "reset button: GPIO %d setup failed (%d); the button does nothing",
           gpio, (int)err);
}

void node_health_log_button_held(uint32_t hold_ms) {
  ESP_LOGW(NODE_HEALTH_LOG_TAG,
           "reset button: held; factory reset in %lu ms unless released",
           (unsigned long)hold_ms);
}

void node_health_log_button_released_early(void) {
  ESP_LOGI(NODE_HEALTH_LOG_TAG, "reset button: released; no factory reset");
}
