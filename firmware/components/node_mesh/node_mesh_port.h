#pragma once

/**
 * @file node_mesh_port.h
 * @brief ESP-IDF services node_mesh needs, apart from Bluetooth itself. Not
 *        part of the public interface.
 *
 * The ESP-IDF build links node_mesh_port.c; host tests link a fake in
 * firmware/test/host/mocks/ that records restarts. The port
 * logs the ESP-IDF error of a failed call itself; callers only see
 * node_mesh_port_err_t. Task context only. The header must not include
 * ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a node_mesh port call. */
typedef enum {
  NODE_MESH_PORT_OK = 0,       /**< Success. */
  NODE_MESH_PORT_ERR_ARG,      /**< NULL pointer from the caller. */
  NODE_MESH_PORT_ERR_PLATFORM, /**< Any other ESP-IDF call failed. */
} node_mesh_port_err_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Read the Bluetooth MAC address from eFuse. Works before Bluetooth
 *        is started.
 *
 * @param mac Receives 6 bytes; must not be NULL.
 * @return NODE_MESH_PORT_OK, NODE_MESH_PORT_ERR_ARG for a NULL @p mac, or
 *         NODE_MESH_PORT_ERR_PLATFORM.
 */
node_mesh_port_err_t node_mesh_port_bt_mac(uint8_t *mac);

/**
 * @brief Free heap right now, in bytes.
 *
 * @param internal Receives the free internal RAM, PSRAM excluded; must not
 *                 be NULL.
 * @param total    Receives the free heap of every kind, PSRAM included;
 *                 must not be NULL.
 * @return NODE_MESH_PORT_OK, or NODE_MESH_PORT_ERR_ARG for a NULL pointer.
 */
node_mesh_port_err_t node_mesh_port_free_heap(size_t *internal, size_t *total);

/**
 * @brief Restart the node. Does not return on the target; the host fake
 *        records the restart and returns.
 */
void node_mesh_port_restart(void);
