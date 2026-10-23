#pragma once

/**
 * @file node_mesh_models_port_mock.h
 * @brief Test controls for the host fake of node_mesh_models_port.c.
 *
 * The fake records every publication and registration instead of handing
 * them to ESP-BLE-MESH. By default the gateway has set a publish address, so
 * publications succeed. Call node_mesh_models_port_mock_reset() from every
 * test's setUp() so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_mesh_err.h"
#include "node_mesh_models_port.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Largest payload the fake records, in bytes. */
#define NODE_MESH_MODELS_PORT_MOCK_MAX_PAYLOAD 200

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One recorded publication. */
typedef struct {
  uint8_t op; /**< Opcode number. */
  size_t len; /**< Payload length in bytes. */
  uint8_t payload[NODE_MESH_MODELS_PORT_MOCK_MAX_PAYLOAD]; /**< Payload. */
} node_mesh_models_port_mock_msg_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded call, and publish and register successfully
 *         again. */
void node_mesh_models_port_mock_reset(void);

/**
 * @brief Make every later publication return @p err without recording it,
 *        such as NODE_MESH_ERR_STATE for "no publish address set", or
 *        NODE_MESH_OK to record them again.
 *
 * @param err Result to return.
 */
void node_mesh_models_port_mock_set_publish_result(node_mesh_err_t err);

/**
 * @brief Make every later registration return @p err.
 *
 * @param err Result to return.
 */
void node_mesh_models_port_mock_set_register_result(node_mesh_err_t err);

/**
 * @brief Number of registrations since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_models_port_mock_register_count(void);

/**
 * @brief Number of publications recorded since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_models_port_mock_publish_count(void);

/**
 * @brief The last publication recorded.
 *
 * @return The publication; all zero if none was recorded.
 */
node_mesh_models_port_mock_msg_t node_mesh_models_port_mock_last_publish(void);
