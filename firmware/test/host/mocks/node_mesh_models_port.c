/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_mesh_err.h"
#include "node_mesh_models_port.h"
#include "node_mesh_models_port_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Result of every publication. */
static node_mesh_err_t g_publish_result = NODE_MESH_OK;

/** @brief Result of every registration. */
static node_mesh_err_t g_register_result = NODE_MESH_OK;

/** @brief Registrations since the last reset. */
static size_t g_register_count = 0;

/** @brief Publications recorded since the last reset. */
static size_t g_publish_count = 0;

/** @brief The last publication recorded; zero if none. */
static node_mesh_models_port_mock_msg_t g_last_publish = {0};

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_mesh_models_port_mock_reset(void) {
  g_publish_result = NODE_MESH_OK;
  g_register_result = NODE_MESH_OK;
  g_register_count = 0;
  g_publish_count = 0;
  (void)memset(&g_last_publish, 0, sizeof(g_last_publish));
}

void node_mesh_models_port_mock_set_publish_result(node_mesh_err_t err) {
  g_publish_result = err;
}

void node_mesh_models_port_mock_set_register_result(node_mesh_err_t err) {
  g_register_result = err;
}

size_t node_mesh_models_port_mock_register_count(void) {
  return g_register_count;
}

size_t node_mesh_models_port_mock_publish_count(void) {
  return g_publish_count;
}

node_mesh_models_port_mock_msg_t node_mesh_models_port_mock_last_publish(void) {
  return g_last_publish;
}

node_mesh_err_t node_mesh_models_port_register(void) {
  g_register_count++;
  return g_register_result;
}

node_mesh_err_t node_mesh_models_port_publish(uint8_t op,
                                              const uint8_t *payload,
                                              size_t len) {
  if (((payload == NULL) && (len > 0u)) ||
      (len > NODE_MESH_MODELS_PORT_MOCK_MAX_PAYLOAD)) {
    return NODE_MESH_ERR_ARG;
  }
  if (g_publish_result != NODE_MESH_OK) {
    return g_publish_result;
  }
  g_publish_count++;
  g_last_publish.op = op;
  g_last_publish.len = len;
  (void)memset(g_last_publish.payload, 0, sizeof(g_last_publish.payload));
  if (len > 0u) {
    (void)memcpy(g_last_publish.payload, payload, len);
  }
  return NODE_MESH_OK;
}
