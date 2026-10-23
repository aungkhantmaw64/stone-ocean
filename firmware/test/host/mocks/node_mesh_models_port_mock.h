#pragma once

/**
 * @file node_mesh_models_port_mock.h
 * @brief Test controls for the host fake of node_mesh_models_port.c.
 *
 * The fake records every publication, reply and registration instead of
 * handing them to ESP-BLE-MESH. Replies are kept in order, up to
 * NODE_MESH_MODELS_PORT_MOCK_MAX_SENT. By default the gateway has set a publish
 * address, so publications succeed. Call node_mesh_models_port_mock_reset()
 * from every test's setUp() so tests stay independent.
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

/** @brief Most replies the fake keeps; one per device of the largest
 *         config, plus one. */
#define NODE_MESH_MODELS_PORT_MOCK_MAX_SENT 9

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One recorded publication or reply. */
typedef struct {
  node_mesh_models_port_ctx_t ctx; /**< Reply: the request's context. Zero for
                                      a publication. */
  uint8_t op;                      /**< Opcode number. */
  size_t len;                      /**< Payload length in bytes. */
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

/**
 * @brief Make every later reply return @p err without recording it, or
 *        NODE_MESH_OK to record them again.
 *
 * @param err Result to return.
 */
void node_mesh_models_port_mock_set_send_result(node_mesh_err_t err);

/**
 * @brief Number of replies recorded since the last reset; replies beyond
 *        NODE_MESH_MODELS_PORT_MOCK_MAX_SENT are counted but not kept.
 *
 * @return The count.
 */
size_t node_mesh_models_port_mock_send_count(void);

/**
 * @brief One recorded reply, in the order they were sent.
 *
 * @param index Position, from 0, below NODE_MESH_MODELS_PORT_MOCK_MAX_SENT.
 * @return The reply; all zero if there is none at @p index.
 */
node_mesh_models_port_mock_msg_t node_mesh_models_port_mock_sent(size_t index);
