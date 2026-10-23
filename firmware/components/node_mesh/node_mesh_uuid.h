#pragma once

/**
 * @file node_mesh_uuid.h
 * @brief Device UUID of the mesh node. Not part of the public interface.
 *
 * Pure logic, built on the target and on the host. The header must not
 * include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>

#include "node_mesh_err.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Length of the mesh device UUID, in bytes. */
#define NODE_MESH_UUID_LEN 16

/** @brief Length of the Bluetooth MAC address, in bytes. */
#define NODE_MESH_UUID_MAC_LEN 6

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Build the mesh device UUID: the ASCII prefix "stoneocean" followed
 *        by the Bluetooth MAC address (spec 10).
 *
 * @param mac  Bluetooth MAC address, NODE_MESH_UUID_MAC_LEN bytes; must not
 *             be NULL.
 * @param uuid Receives NODE_MESH_UUID_LEN bytes; must not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer.
 */
node_mesh_err_t node_mesh_uuid_build(const uint8_t *mac, uint8_t *uuid);
