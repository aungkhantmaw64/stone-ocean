/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_mesh_internal.h"
#include "node_mesh_log.h"
#include "node_mesh_log_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Provisioning-complete lines logged since the last reset. */
static size_t g_prov_complete_count = 0;

/** @brief Arguments of the last provisioning-complete line; zero if none. */
static node_mesh_log_mock_prov_t g_last_prov = {0};

/** @brief "Node reset by the gateway" lines logged since the last reset. */
static size_t g_node_reset_count = 0;

/** @brief "Mesh event lost" lines logged since the last reset. */
static size_t g_post_failed_count = 0;

/** @brief Provisioning notices logged since the last reset. */
static size_t g_notice_count = 0;

/** @brief The last provisioning notice logged. */
static node_mesh_ao_notice_t g_last_notice = NODE_MESH_AO_NOTICE_LINK_OPEN;

/** @brief Error code of the last provisioning notice; 0 if none. */
static int32_t g_last_notice_err = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_mesh_log_mock_reset(void) {
  g_prov_complete_count = 0;
  g_last_prov = (node_mesh_log_mock_prov_t){0};
  g_node_reset_count = 0;
  g_post_failed_count = 0;
  g_notice_count = 0;
  g_last_notice = NODE_MESH_AO_NOTICE_LINK_OPEN;
  g_last_notice_err = 0;
}

size_t node_mesh_log_mock_prov_complete_count(void) {
  return g_prov_complete_count;
}

node_mesh_log_mock_prov_t node_mesh_log_mock_last_prov_complete(void) {
  return g_last_prov;
}

size_t node_mesh_log_mock_node_reset_count(void) { return g_node_reset_count; }

size_t node_mesh_log_mock_post_failed_count(void) {
  return g_post_failed_count;
}

size_t node_mesh_log_mock_notice_count(void) { return g_notice_count; }

node_mesh_ao_notice_t node_mesh_log_mock_last_notice(void) {
  return g_last_notice;
}

int32_t node_mesh_log_mock_last_notice_err(void) { return g_last_notice_err; }

void node_mesh_log_prov_complete(uint16_t addr, uint8_t element_count,
                                 bool relay, uint16_t net_idx,
                                 uint32_t iv_index) {
  g_prov_complete_count++;
  g_last_prov = (node_mesh_log_mock_prov_t){
      .addr = addr,
      .element_count = element_count,
      .relay = relay,
      .net_idx = net_idx,
      .iv_index = iv_index,
  };
}

void node_mesh_log_node_reset(void) { g_node_reset_count++; }

void node_mesh_log_post_failed(const char *what) {
  (void)what;
  g_post_failed_count++;
}

void node_mesh_log_notice(node_mesh_ao_notice_t notice, int32_t err) {
  g_notice_count++;
  g_last_notice = notice;
  g_last_notice_err = err;
}
