/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
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

/** @brief True if a composition hash is stored. */
static bool g_hash_stored = false;

/** @brief The stored composition hash; valid while g_hash_stored. */
static uint32_t g_hash = 0;

/** @brief True if hash reads fail. */
static bool g_fail_load = false;

/** @brief True if hash writes fail. */
static bool g_fail_store = false;

/** @brief True if `meshmeta` erases fail. */
static bool g_fail_erase = false;

/** @brief `meshmeta` erases attempted since the last reset. */
static size_t g_erase_count = 0;

/** @brief Restarts since the last reset. */
static size_t g_restart_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_mesh_port_mock_reset(void) {
  g_hash_stored = false;
  g_hash = 0;
  g_fail_load = false;
  g_fail_store = false;
  g_fail_erase = false;
  g_erase_count = 0;
  g_restart_count = 0;
}

void node_mesh_port_mock_set_hash(uint32_t hash) {
  g_hash_stored = true;
  g_hash = hash;
}

bool node_mesh_port_mock_hash_is_stored(void) { return g_hash_stored; }

uint32_t node_mesh_port_mock_hash(void) { return g_hash_stored ? g_hash : 0; }

void node_mesh_port_mock_fail_hash_load(void) { g_fail_load = true; }

void node_mesh_port_mock_fail_hash_store(void) { g_fail_store = true; }

void node_mesh_port_mock_fail_meta_erase(void) { g_fail_erase = true; }

size_t node_mesh_port_mock_meta_erase_count(void) { return g_erase_count; }

size_t node_mesh_port_mock_restart_count(void) { return g_restart_count; }

node_mesh_port_err_t node_mesh_port_hash_load(uint32_t *out) {
  if (out == NULL) {
    return NODE_MESH_PORT_ERR_ARG;
  }
  *out = 0;
  if (g_fail_load) {
    return NODE_MESH_PORT_ERR_IO;
  }
  if (!g_hash_stored) {
    return NODE_MESH_PORT_NOT_FOUND;
  }
  *out = g_hash;
  return NODE_MESH_PORT_OK;
}

node_mesh_port_err_t node_mesh_port_hash_store(uint32_t hash) {
  if (g_fail_store) {
    return NODE_MESH_PORT_ERR_IO;
  }
  node_mesh_port_mock_set_hash(hash);
  return NODE_MESH_PORT_OK;
}

node_mesh_port_err_t node_mesh_port_meta_erase(void) {
  g_erase_count++;
  if (g_fail_erase) {
    return NODE_MESH_PORT_ERR_IO;
  }
  g_hash_stored = false;
  g_hash = 0;
  return NODE_MESH_PORT_OK;
}

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
