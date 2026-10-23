#pragma once

/**
 * @file node_mesh_models_port.h
 * @brief ESP-BLE-MESH side of the Stone Ocean Node Server vendor model. Not
 *        part of the public interface.
 *
 * The ESP-IDF build links node_mesh_models_port.c, which owns the vendor
 * model and its publication context. Host tests link a fake in
 * firmware/test/host/mocks/ that records what is published. The port logs
 * ESP-BLE-MESH errors itself. The header must not include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_mesh_err.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Register the vendor model with node_mesh_set_vendor_models().
 *
 * Task context, before node_mesh_start().
 *
 * @return NODE_MESH_OK, or NODE_MESH_ERR_STATE if the mesh is already
 *         started.
 */
node_mesh_err_t node_mesh_models_port_register(void);

/**
 * @brief Publish a message from the vendor model to its publish address.
 *
 * Task context; called from the models AO. Never blocks: ESP-BLE-MESH
 * queues the message.
 *
 * @param op      Opcode number, one of NODE_MESH_MSG_OP_*; the port adds the
 *                vendor opcode bits and the company ID.
 * @param payload Message payload; may be NULL if @p len is 0.
 * @param len     Payload length in bytes, at most
 *                NODE_MESH_MSG_MAX_UNSEGMENTED.
 * @return NODE_MESH_OK if the message was handed to ESP-BLE-MESH;
 *         NODE_MESH_ERR_STATE if nothing was sent because the node is not
 *         provisioned or the gateway set no publish address;
 *         NODE_MESH_ERR_ARG for a bad payload; or NODE_MESH_ERR_MESH if
 *         ESP-BLE-MESH refused it.
 */
node_mesh_err_t node_mesh_models_port_publish(uint8_t op,
                                              const uint8_t *payload,
                                              size_t len);
