/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_mesh_port.h"
#include "node_mesh_port_mock.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Length of the fake Bluetooth MAC address, in bytes. */
#define NODE_MESH_PORT_MOCK_MAC_LEN 6

/** @brief Free internal RAM the fake reports, in bytes. */
#define NODE_MESH_PORT_MOCK_FREE_INTERNAL (64u * 1024u)

/** @brief Free heap of every kind the fake reports, in bytes. */
#define NODE_MESH_PORT_MOCK_FREE_TOTAL (4u * 1024u * 1024u)

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Bluetooth MAC address the fake reports. */
static const uint8_t g_mac[NODE_MESH_PORT_MOCK_MAC_LEN] = {0x24, 0x6f, 0x28,
                                                           0x01, 0x02, 0x03};

/** @brief Restarts since the last reset. */
static size_t g_restart_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_mesh_port_mock_reset(void) { g_restart_count = 0; }

size_t node_mesh_port_mock_restart_count(void) { return g_restart_count; }

node_mesh_port_err_t node_mesh_port_bt_mac(uint8_t *mac) {
  if (mac == NULL) {
    return NODE_MESH_PORT_ERR_ARG;
  }
  (void)memcpy(mac, g_mac, sizeof(g_mac));
  return NODE_MESH_PORT_OK;
}

node_mesh_port_err_t node_mesh_port_free_heap(size_t *internal, size_t *total) {
  if ((internal == NULL) || (total == NULL)) {
    return NODE_MESH_PORT_ERR_ARG;
  }
  *internal = NODE_MESH_PORT_MOCK_FREE_INTERNAL;
  *total = NODE_MESH_PORT_MOCK_FREE_TOTAL;
  return NODE_MESH_PORT_OK;
}

void node_mesh_port_restart(void) { g_restart_count++; }
