/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_port.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_mesh port. */
#define NODE_MESH_PORT_LOG_TAG "node_mesh"

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Log a failed ESP-IDF call at WARNING level.
 *
 * @param what NUL-terminated name of the call.
 * @param err  Its error.
 */
static void node_mesh_port_log_error(const char *what, esp_err_t err);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_port_err_t node_mesh_port_bt_mac(uint8_t *mac) {
  if (mac == NULL) {
    return NODE_MESH_PORT_ERR_ARG;
  }
  const esp_err_t err = esp_read_mac(mac, ESP_MAC_BT);
  if (err != ESP_OK) {
    node_mesh_port_log_error("esp_read_mac", err);
    return NODE_MESH_PORT_ERR_PLATFORM;
  }
  return NODE_MESH_PORT_OK;
}

node_mesh_port_err_t node_mesh_port_free_heap(size_t *internal, size_t *total) {
  if ((internal == NULL) || (total == NULL)) {
    return NODE_MESH_PORT_ERR_ARG;
  }
  *internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  *total = (size_t)esp_get_free_heap_size();
  return NODE_MESH_PORT_OK;
}

void node_mesh_port_restart(void) { esp_restart(); }

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_mesh_port_log_error(const char *what, esp_err_t err) {
  ESP_LOGW(NODE_MESH_PORT_LOG_TAG, "%s failed: %s", what, esp_err_to_name(err));
}
