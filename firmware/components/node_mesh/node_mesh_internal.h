#pragma once

/**
 * @file node_mesh_internal.h
 * @brief Shared between node_mesh's source files, plus hooks for host tests.
 *        Not part of the public interface.
 *
 * The mesh AO handles provisioning events (spec 10). ESP-BLE-MESH runs its
 * callbacks in Bluedroid's task; they only post events to the mesh AO
 * through the functions below. The header must not include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "node_mesh_err.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Provisioning progress that the mesh AO only logs. */
typedef enum {
  NODE_MESH_AO_NOTICE_LINK_OPEN,          /**< A provisioner opened a PB-ADV
                                             link. */
  NODE_MESH_AO_NOTICE_LINK_CLOSE,         /**< The PB-ADV link closed. */
  NODE_MESH_AO_NOTICE_PROV_ENABLE_FAILED, /**< Unprovisioned beacons could
                                             not be started. */
} node_mesh_ao_notice_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Start the mesh AO.
 *
 * Called once by node_mesh_start(), from task context, before ESP-BLE-MESH
 * is initialised, so no callback can post before the AO exists.
 *
 * @param element_count Elements registered this boot, for the log.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_STATE if it is already started or
 *         could not be started.
 */
node_mesh_err_t node_mesh_ao_start(uint8_t element_count);

/**
 * @brief Tell the mesh AO that provisioning completed. Never blocks.
 *
 * Task context, such as Bluedroid's task. The AO logs the new state. A
 * failure to post is logged.
 *
 * @param net_idx  Index of the network key the node was given.
 * @param addr     Unicast address of the primary element.
 * @param iv_index IV index of the network.
 * @param relay    True if the relay feature is enabled.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_STATE if the AO is not started or
 *         its queue is full.
 */
node_mesh_err_t node_mesh_ao_post_prov_complete(uint16_t net_idx, uint16_t addr,
                                                uint32_t iv_index, bool relay);

/**
 * @brief Tell the mesh AO that the gateway reset the node (Config Node
 *        Reset). Never blocks.
 *
 * Task context, such as Bluedroid's task. The AO restarts the node:
 * ESP-BLE-MESH does not send unprovisioned beacons again after a node
 * reset, and a restart does. A failure to post is logged.
 *
 * @return NODE_MESH_OK, or NODE_MESH_ERR_STATE if the AO is not started or
 *         its queue is full.
 */
node_mesh_err_t node_mesh_ao_post_node_reset(void);

/**
 * @brief Tell the mesh AO about provisioning progress, which it logs. Never
 *        blocks.
 *
 * Task context, such as Bluedroid's task. A failure to post is logged.
 *
 * @param notice What happened.
 * @param err    ESP-BLE-MESH error code for
 *               NODE_MESH_AO_NOTICE_PROV_ENABLE_FAILED; 0 otherwise.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_STATE if the AO is not started or
 *         its queue is full.
 */
node_mesh_err_t node_mesh_ao_post_notice(node_mesh_ao_notice_t notice,
                                         int32_t err);

/**
 * @brief Forget that the mesh AO was started and clear its state.
 *
 * Host tests only, after node_ao_port_mock_reset(), while no thread uses
 * the mesh AO.
 */
void node_mesh_ao_reset(void);
