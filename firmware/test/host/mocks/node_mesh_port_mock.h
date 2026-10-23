#pragma once

/**
 * @file node_mesh_port_mock.h
 * @brief Test controls for the host fake of node_mesh_port.c.
 *
 * The fake keeps the `meshmeta` composition hash in memory, can be told to
 * fail each NVS operation, and records restarts instead of restarting. Call
 * node_mesh_port_mock_reset() from every test's setUp() so tests stay
 * independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_mesh_port.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget the stored hash, every injected failure and every recorded
 *         restart. */
void node_mesh_port_mock_reset(void);

/**
 * @brief Store a composition hash, as a provisioning on an earlier boot
 *        would have.
 *
 * @param hash Hash to store.
 */
void node_mesh_port_mock_set_hash(uint32_t hash);

/**
 * @brief Whether a composition hash is stored.
 *
 * @return True if stored.
 */
bool node_mesh_port_mock_hash_is_stored(void);

/**
 * @brief The stored composition hash.
 *
 * @return The hash; 0 if none is stored.
 */
uint32_t node_mesh_port_mock_hash(void);

/** @brief Make every later hash read fail with NODE_MESH_PORT_ERR_IO. */
void node_mesh_port_mock_fail_hash_load(void);

/** @brief Make every later hash write fail with NODE_MESH_PORT_ERR_IO. */
void node_mesh_port_mock_fail_hash_store(void);

/** @brief Make every later `meshmeta` erase fail with
 *         NODE_MESH_PORT_ERR_IO. */
void node_mesh_port_mock_fail_meta_erase(void);

/**
 * @brief Number of `meshmeta` erases attempted since the last reset, failed
 *        ones included.
 *
 * @return The count.
 */
size_t node_mesh_port_mock_meta_erase_count(void);

/**
 * @brief Number of restarts since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_port_mock_restart_count(void);
