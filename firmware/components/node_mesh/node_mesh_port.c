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
#include "nvs.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_mesh port. */
#define NODE_MESH_PORT_LOG_TAG "node_mesh"

/** @brief NVS namespace that holds node_mesh's data (spec 10). */
#define NODE_MESH_PORT_NAMESPACE "meshmeta"

/** @brief NVS key of the composition hash, a uint32_t. */
#define NODE_MESH_PORT_KEY_COMP_HASH "comp_hash"

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

node_mesh_port_err_t node_mesh_port_hash_load(uint32_t *out) {
  if (out == NULL) {
    return NODE_MESH_PORT_ERR_ARG;
  }
  *out = 0;
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(NODE_MESH_PORT_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return NODE_MESH_PORT_NOT_FOUND;
  }
  if (err != ESP_OK) {
    node_mesh_port_log_error("nvs_open", err);
    return NODE_MESH_PORT_ERR_IO;
  }
  uint32_t hash = 0;
  err = nvs_get_u32(handle, NODE_MESH_PORT_KEY_COMP_HASH, &hash);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return NODE_MESH_PORT_NOT_FOUND;
  }
  if (err != ESP_OK) {
    node_mesh_port_log_error("nvs_get_u32", err);
    return NODE_MESH_PORT_ERR_IO;
  }
  *out = hash;
  return NODE_MESH_PORT_OK;
}

node_mesh_port_err_t node_mesh_port_hash_store(uint32_t hash) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(NODE_MESH_PORT_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    node_mesh_port_log_error("nvs_open", err);
    return NODE_MESH_PORT_ERR_IO;
  }
  err = nvs_set_u32(handle, NODE_MESH_PORT_KEY_COMP_HASH, hash);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    node_mesh_port_log_error("nvs_set_u32", err);
    return NODE_MESH_PORT_ERR_IO;
  }
  return NODE_MESH_PORT_OK;
}

node_mesh_port_err_t node_mesh_port_meta_erase(void) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(NODE_MESH_PORT_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    node_mesh_port_log_error("nvs_open", err);
    return NODE_MESH_PORT_ERR_IO;
  }
  err = nvs_erase_all(handle);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    node_mesh_port_log_error("nvs_erase_all", err);
    return NODE_MESH_PORT_ERR_IO;
  }
  return NODE_MESH_PORT_OK;
}

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
