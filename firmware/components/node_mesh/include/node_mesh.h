#pragma once

/**
 * @file node_mesh.h
 * @brief BLE mesh node core (spec 10): Bluetooth bring-up, composition,
 *        provisioning and the mesh state kept across reboots.
 *
 * node_mesh_start() brings the node up as a mesh device that a gateway
 * provisions over PB-ADV. Its composition is one element, the primary one,
 * with the Config Server, the Health Server and the vendor models spec 11
 * registers before the start. It does not depend on the config, so a config
 * change needs no reprovisioning.
 *
 * Only built when CONFIG_NODE_MESH_ENABLE is set (the hardware build); the
 * QEMU build has no Bluetooth.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "esp_ble_mesh_defs.h"
#include "node_mesh_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Start Bluetooth and the mesh node.
 *
 * Call once, from the boot task, after node_health_init() has initialised
 * NVS, in normal and safe mode alike. Starts the Bluetooth controller in
 * BLE-only mode and the Bluedroid host, builds the composition, initialises
 * ESP-BLE-MESH, which restores a provisioned node's address and keys, and,
 * if the node is unprovisioned, starts sending unprovisioned beacons on
 * PB-ADV. Also registers the factory-reset hook and logs the mesh state.
 * Returns within 2 s.
 *
 * @return NODE_MESH_OK, NODE_MESH_ERR_STATE if already started,
 *         NODE_MESH_ERR_BT or NODE_MESH_ERR_MESH.
 */
node_mesh_err_t node_mesh_start(void);

/**
 * @brief Whether the node is provisioned into a mesh network.
 *
 * Task context.
 *
 * @return True if provisioned; false before node_mesh_start() succeeded.
 */
bool node_mesh_is_provisioned(void);

/**
 * @brief Unicast address of the primary element.
 *
 * Task context.
 *
 * @return The address, or 0 (unassigned) if the node is not provisioned.
 */
uint16_t node_mesh_primary_addr(void);

/**
 * @brief Register the vendor models of the primary element (spec 11).
 *
 * Call before node_mesh_start(), from task context. A second call replaces
 * the models.
 *
 * @param models Vendor model array; must not be NULL and must stay valid for
 *               as long as the node runs.
 * @param count  Number of models in @p models, at least 1.
 * @return NODE_MESH_OK, NODE_MESH_ERR_ARG for a NULL @p models or a zero
 *         @p count, or NODE_MESH_ERR_STATE if the mesh is already started.
 */
node_mesh_err_t node_mesh_set_vendor_models(esp_ble_mesh_model_t *models,
                                            uint8_t count);

#ifdef __cplusplus
}
#endif
