/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_health_port.h"

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_health port. */
#define NODE_HEALTH_PORT_LOG_TAG "node_health"

/** @brief NVS namespace that holds node_health's data (spec 09). */
#define NODE_HEALTH_PORT_NAMESPACE "health"

/** @brief NVS key of the crash counter, a uint32_t. */
#define NODE_HEALTH_PORT_KEY_CRASH_COUNT "crash_count"

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Log a failed ESP-IDF call at WARNING level.
 *
 * @param what NUL-terminated name of the call.
 * @param err  Its error.
 */
static void node_health_port_log_error(const char *what, esp_err_t err);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_health_port_nvs_t node_health_port_nvs_init(void) {
  const esp_err_t err = nvs_flash_init();
  switch (err) {
    case ESP_OK:
      return NODE_HEALTH_PORT_NVS_OK;
    case ESP_ERR_NVS_NO_FREE_PAGES:
      return NODE_HEALTH_PORT_NVS_NO_FREE;
    case ESP_ERR_NVS_NEW_VERSION_FOUND:
      return NODE_HEALTH_PORT_NVS_NEW_VERSION;
    default:
      node_health_port_log_error("nvs_flash_init", err);
      return NODE_HEALTH_PORT_NVS_FAILED;
  }
}

node_health_err_t node_health_port_nvs_erase(void) {
  const esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) {
    node_health_port_log_error("nvs_flash_erase", err);
    return NODE_HEALTH_ERR_IO;
  }
  return NODE_HEALTH_OK;
}

node_health_port_reason_t node_health_port_reset_reason(void) {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return NODE_HEALTH_PORT_REASON_POWERON;
    case ESP_RST_EXT:
      return NODE_HEALTH_PORT_REASON_EXT;
    case ESP_RST_SW:
      return NODE_HEALTH_PORT_REASON_SW;
    case ESP_RST_PANIC:
      return NODE_HEALTH_PORT_REASON_PANIC;
    case ESP_RST_INT_WDT:
      return NODE_HEALTH_PORT_REASON_INT_WDT;
    case ESP_RST_TASK_WDT:
      return NODE_HEALTH_PORT_REASON_TASK_WDT;
    case ESP_RST_WDT:
      return NODE_HEALTH_PORT_REASON_WDT;
    case ESP_RST_DEEPSLEEP:
      return NODE_HEALTH_PORT_REASON_DEEPSLEEP;
    case ESP_RST_BROWNOUT:
      return NODE_HEALTH_PORT_REASON_BROWNOUT;
    case ESP_RST_SDIO:
      return NODE_HEALTH_PORT_REASON_SDIO;
    default:
      return NODE_HEALTH_PORT_REASON_UNKNOWN;
  }
}

node_health_err_t node_health_port_counter_load(uint32_t *out) {
  if (out == NULL) {
    return NODE_HEALTH_ERR_ARG;
  }
  *out = 0;
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(NODE_HEALTH_PORT_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return NODE_HEALTH_OK;
  }
  if (err != ESP_OK) {
    node_health_port_log_error("nvs_open", err);
    return NODE_HEALTH_ERR_IO;
  }
  err = nvs_get_u32(handle, NODE_HEALTH_PORT_KEY_CRASH_COUNT, out);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *out = 0;
    return NODE_HEALTH_OK;
  }
  if (err != ESP_OK) {
    node_health_port_log_error("nvs_get_u32", err);
    return NODE_HEALTH_ERR_IO;
  }
  return NODE_HEALTH_OK;
}

node_health_err_t node_health_port_counter_store(uint32_t count) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(NODE_HEALTH_PORT_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    node_health_port_log_error("nvs_open", err);
    return NODE_HEALTH_ERR_IO;
  }
  err = nvs_set_u32(handle, NODE_HEALTH_PORT_KEY_CRASH_COUNT, count);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    node_health_port_log_error("nvs_set_u32", err);
    return NODE_HEALTH_ERR_IO;
  }
  return NODE_HEALTH_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_health_port_log_error(const char *what, esp_err_t err) {
  ESP_LOGW(NODE_HEALTH_PORT_LOG_TAG, "%s failed: %s", what,
           esp_err_to_name(err));
}
