/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "node_config.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_mesh component. */
#define NODE_MESH_LOG_TAG "node_mesh"

/** @brief Size of the text buffer of one list of key indexes, including the
 *         NUL; room for 8 indexes of the form "0x123,". */
#define NODE_MESH_LOG_IDX_TEXT_LEN 48

/** @brief Size of the text buffer of a UUID in hex, including the NUL. */
#define NODE_MESH_LOG_UUID_TEXT_LEN 33

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Name of a device type, as written in the config.
 *
 * @param type The type.
 * @return A NUL-terminated name; "unknown" for an out-of-range value.
 */
static const char *node_mesh_log_type_name(node_config_dev_type_t type);

/**
 * @brief Write key indexes as comma-separated hex into a buffer; "none" if
 *        there are none. Indexes that do not fit are left out.
 *
 * @param idx  Indexes; may be NULL if @p n is 0.
 * @param n    Number of entries in @p idx.
 * @param text Receives the NUL-terminated text; must not be NULL.
 * @param cap  Size of @p text in bytes; at least 5.
 */
static void node_mesh_log_format_idx(const uint16_t *idx, size_t n, char *text,
                                     size_t cap);

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

void node_mesh_log_notice(node_mesh_ao_notice_t notice, int32_t err) {
  switch (notice) {
    case NODE_MESH_AO_NOTICE_LINK_OPEN:
      ESP_LOGI(NODE_MESH_LOG_TAG, "provisioning link opened (PB-ADV)");
      break;
    case NODE_MESH_AO_NOTICE_LINK_CLOSE:
      ESP_LOGI(NODE_MESH_LOG_TAG, "provisioning link closed (PB-ADV)");
      break;
    case NODE_MESH_AO_NOTICE_PROV_ENABLE_FAILED:
      ESP_LOGE(NODE_MESH_LOG_TAG, "unprovisioned beacons not started: %ld",
               (long)err);
      break;
    default:
      ESP_LOGW(NODE_MESH_LOG_TAG, "unknown provisioning notice %d",
               (int)notice);
      break;
  }
}

void node_mesh_log_mismatch(uint32_t stored_hash, uint32_t config_hash) {
  ESP_LOGW(NODE_MESH_LOG_TAG,
           "config composition changed since provisioning; factory-reset "
           "and reprovision required");
  ESP_LOGW(NODE_MESH_LOG_TAG,
           "stored hash 0x%08lx, config hash 0x%08lx; device elements "
           "disabled",
           (unsigned long)stored_hash, (unsigned long)config_hash);
}

void node_mesh_log_hash_load_failed(void) {
  ESP_LOGW(NODE_MESH_LOG_TAG,
           "composition hash unreadable; treating the node as never "
           "provisioned");
}

void node_mesh_log_hash_missing(void) {
  ESP_LOGW(NODE_MESH_LOG_TAG,
           "provisioned but no composition hash stored; storing the one in "
           "use");
}

void node_mesh_log_hash_stale(void) {
  ESP_LOGW(NODE_MESH_LOG_TAG,
           "composition hash stored but not provisioned; erasing it");
}

void node_mesh_log_builder_failed(int dev_index, node_mesh_err_t err) {
  ESP_LOGE(NODE_MESH_LOG_TAG, "device %d: models not built (%d); no models",
           dev_index, (int)err);
}

void node_mesh_log_call_failed(const char *what, int32_t err) {
  ESP_LOGE(NODE_MESH_LOG_TAG, "%s failed: %s", (what != NULL) ? what : "?",
           esp_err_to_name((esp_err_t)err));
}

void node_mesh_log_hook_failed(void) {
  ESP_LOGE(NODE_MESH_LOG_TAG,
           "factory-reset hook not registered; a factory reset will leave "
           "the node provisioned");
}

void node_mesh_log_uuid(const uint8_t *uuid, size_t len) {
  if (uuid == NULL) {
    return;
  }
  char text[NODE_MESH_LOG_UUID_TEXT_LEN] = {0};
  size_t used = 0;
  for (size_t i = 0; (i < len) && ((used + 2u) < sizeof(text)); i++) {
    const int written =
        snprintf(&text[used], sizeof(text) - used, "%02x", (unsigned)uuid[i]);
    if (written != 2) {
      break;
    }
    used += 2u;
  }
  ESP_LOGI(NODE_MESH_LOG_TAG, "uuid=%s", text);
}

void node_mesh_log_state(bool provisioned, uint16_t addr, uint8_t element_count,
                         bool relay) {
  ESP_LOGI(NODE_MESH_LOG_TAG, "mesh=%s addr=0x%04x elements=%u relay=%s",
           provisioned ? "prov" : "unprov", (unsigned)addr,
           (unsigned)element_count, relay ? "on" : "off");
}

void node_mesh_log_keys(uint32_t iv_index, const uint16_t *net_idx,
                        size_t n_net, const uint16_t *app_idx, size_t n_app) {
  char net_text[NODE_MESH_LOG_IDX_TEXT_LEN] = {0};
  char app_text[NODE_MESH_LOG_IDX_TEXT_LEN] = {0};
  node_mesh_log_format_idx(net_idx, n_net, net_text, sizeof(net_text));
  node_mesh_log_format_idx(app_idx, n_app, app_text, sizeof(app_text));
  ESP_LOGI(NODE_MESH_LOG_TAG, "iv_index=%lu net_idx=%s app_idx=%s",
           (unsigned long)iv_index, net_text, app_text);
}

void node_mesh_log_primary_element(uint8_t model_count) {
  ESP_LOGI(NODE_MESH_LOG_TAG,
           "element 0: primary, config + health server (%u models)",
           (unsigned)model_count);
}

void node_mesh_log_device_element(uint8_t element, const node_config_dev_t *dev,
                                  uint8_t model_count) {
  if (dev == NULL) {
    return;
  }
  ESP_LOGI(NODE_MESH_LOG_TAG, "element %u: %s (%s, %u models)",
           (unsigned)element, dev->id, node_mesh_log_type_name(dev->type),
           (unsigned)model_count);
}

void node_mesh_log_heap(size_t internal, size_t total, bool low) {
  if (low) {
    ESP_LOGE(NODE_MESH_LOG_TAG,
             "free heap after mesh start: internal=%u total=%u; internal is "
             "below 40 KB",
             (unsigned)internal, (unsigned)total);
    return;
  }
  ESP_LOGI(NODE_MESH_LOG_TAG,
           "free heap after mesh start: internal=%u total=%u",
           (unsigned)internal, (unsigned)total);
}

void node_mesh_log_started(uint32_t elapsed_ms) {
  ESP_LOGI(NODE_MESH_LOG_TAG, "mesh started in %lu ms",
           (unsigned long)elapsed_ms);
}

void node_mesh_log_reset_timeout(void) {
  ESP_LOGE(NODE_MESH_LOG_TAG,
           "mesh reset did not finish in time; factory reset goes on");
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static const char *node_mesh_log_type_name(node_config_dev_type_t type) {
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

static void node_mesh_log_format_idx(const uint16_t *idx, size_t n, char *text,
                                     size_t cap) {
  if ((idx == NULL) || (n == 0u)) {
    (void)snprintf(text, cap, "none");
    return;
  }
  text[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < n; i++) {
    const int written = snprintf(&text[used], cap - used, "%s0x%03x",
                                 (i == 0u) ? "" : ",", (unsigned)idx[i]);
    if ((written < 0) || ((size_t)written >= (cap - used))) {
      text[used] = '\0';
      return;
    }
    used += (size_t)written;
  }
}
