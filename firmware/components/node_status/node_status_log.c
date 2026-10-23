/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_status_log.h"

#include <stdbool.h>

#include "esp_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_status component. */
#define NODE_STATUS_LOG_TAG "node_status"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_status_log_started(int gpio) {
  ESP_LOGI(NODE_STATUS_LOG_TAG, "status led: started on GPIO %d, off", gpio);
}

void node_status_log_setup_failed(int gpio, node_hal_err_t err) {
  ESP_LOGW(NODE_STATUS_LOG_TAG,
           "status led: GPIO %d setup failed (%d); commands are ignored", gpio,
           (int)err);
}

void node_status_log_steady(bool on) {
  ESP_LOGI(NODE_STATUS_LOG_TAG, "status led: %s", on ? "on" : "off");
}

void node_status_log_write_failed(int gpio, node_hal_err_t err) {
  ESP_LOGW(NODE_STATUS_LOG_TAG, "status led: writes to GPIO %d fail (%d)", gpio,
           (int)err);
}

void node_status_log_write_recovered(int gpio) {
  ESP_LOGI(NODE_STATUS_LOG_TAG, "status led: writes to GPIO %d work again",
           gpio);
}
