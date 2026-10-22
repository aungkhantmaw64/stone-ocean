/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_dev component. */
#define NODE_DEV_LOG_TAG "node_dev"

/** @brief Size of the buffer a warning is formatted into, including the
 *         terminating NUL; longer warnings are truncated. */
#define NODE_DEV_LOG_WARNING_LEN 128

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Name of a device type, as written in the JSON config.
 *
 * @param type Device type.
 * @return A NUL-terminated name, or "unknown".
 */
static const char *node_dev_log_type_name(node_config_dev_type_t type);

/**
 * @brief Name of a device state.
 *
 * @param fault True if the device is in FAULT.
 * @return "FAULT" or "ok".
 */
static const char *node_dev_log_state_name(bool fault);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_dev_log_table_title(int n_dev) {
  ESP_LOGI(NODE_DEV_LOG_TAG, "%d device(s):", n_dev);
}

void node_dev_log_table_row(int dev_index, const node_config_dev_t *dev,
                            bool fault) {
  if (dev == NULL) {
    return;
  }
  ESP_LOGI(NODE_DEV_LOG_TAG, "  [%d] id '%s' type %s state %s", dev_index,
           dev->id, node_dev_log_type_name(dev->type),
           node_dev_log_state_name(fault));
}

void node_dev_log_state_change(int dev_index, const char *id, bool fault) {
  const char *const name = (id != NULL) ? id : "";
  if (fault) {
    ESP_LOGW(NODE_DEV_LOG_TAG, "[%d] '%s' state ok -> FAULT", dev_index, name);
  } else {
    ESP_LOGI(NODE_DEV_LOG_TAG, "[%d] '%s' state FAULT -> ok", dev_index, name);
  }
}

void node_dev_log_event(const char *id, const node_dev_evt_t *evt) {
  if (evt == NULL) {
    return;
  }
  const char *const name = (id != NULL) ? id : "";
  switch (evt->super.sig) {
    case NODE_DEV_SIG_READING:
      ESP_LOGI(NODE_DEV_LOG_TAG, "[%u] '%s' reading temp=%.2f C rh=%.2f %%",
               (unsigned)evt->dev_index, name, (double)evt->value.temp_c,
               (double)evt->value.rh_pct);
      break;
    case NODE_DEV_SIG_MOTION:
      ESP_LOGI(NODE_DEV_LOG_TAG, "[%u] '%s' motion=%d",
               (unsigned)evt->dev_index, name, evt->value.motion ? 1 : 0);
      break;
    case NODE_DEV_SIG_ONOFF_CHANGED:
      ESP_LOGI(NODE_DEV_LOG_TAG, "[%u] '%s' on=%d", (unsigned)evt->dev_index,
               name, evt->value.on ? 1 : 0);
      break;
    default:
      break;
  }
}

void node_dev_log_warning(const char *fmt, ...) {
  if (fmt == NULL) {
    return;
  }
  char text[NODE_DEV_LOG_WARNING_LEN] = {0};
  va_list args;
  va_start(args, fmt);
  /* Truncating an over-long warning is acceptable for a log line. */
  (void)vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  ESP_LOGW(NODE_DEV_LOG_TAG, "%s", text);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static const char *node_dev_log_type_name(node_config_dev_type_t type) {
  switch (type) {
    case NODE_CONFIG_DEV_SHTC3:
      return "shtc3";
    case NODE_CONFIG_DEV_PIR:
      return "pir";
    case NODE_CONFIG_DEV_LED:
      return "led";
    default:
      return "unknown";
  }
}

static const char *node_dev_log_state_name(bool fault) {
  return fault ? "FAULT" : "ok";
}
