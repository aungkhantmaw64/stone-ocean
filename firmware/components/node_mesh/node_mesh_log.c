/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_mesh component. */
#define NODE_MESH_LOG_TAG "node_mesh"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_mesh_log_prov_complete(uint16_t addr, uint8_t element_count,
                                 bool relay, uint16_t net_idx,
                                 uint32_t iv_index) {
  ESP_LOGI(NODE_MESH_LOG_TAG, "mesh=prov addr=0x%04x elements=%u relay=%s",
           (unsigned)addr, (unsigned)element_count, relay ? "on" : "off");
  ESP_LOGI(NODE_MESH_LOG_TAG, "net_idx=0x%03x iv_index=%lu", (unsigned)net_idx,
           (unsigned long)iv_index);
}

void node_mesh_log_hash_store_failed(void) {
  ESP_LOGE(NODE_MESH_LOG_TAG,
           "composition hash not stored; a config change will not be "
           "detected");
}

void node_mesh_log_node_reset(void) {
  ESP_LOGW(NODE_MESH_LOG_TAG,
           "node reset by the gateway; erasing mesh metadata and restarting");
}

void node_mesh_log_meta_erase_failed(void) {
  ESP_LOGE(NODE_MESH_LOG_TAG, "mesh metadata erase failed");
}

void node_mesh_log_post_failed(const char *what) {
  ESP_LOGE(NODE_MESH_LOG_TAG, "mesh event lost: %s",
           (what != NULL) ? what : "?");
}
