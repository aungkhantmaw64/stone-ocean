#pragma once

/**
 * @file node_mesh.h
 * @brief BLE mesh node core (spec 10): Bluetooth bring-up, composition,
 *        provisioning and the mesh state kept across reboots.
 *
 * node_mesh_start() brings the node up as a mesh device that a gateway
 * provisions over PB-ADV. Its composition has a primary element (Config
 * Server and Health Server) and, in normal mode, one element per configured
 * device, in config order. Spec 11 puts the device models on those elements
 * through a models builder registered before the start.
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
#include "node_config.h"
#include "node_mesh_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/**
 * @brief Gives the SIG models of one device's element (spec 11).
 *
 * Called by node_mesh_start(), in the boot task, once for every device that
 * has an element, before ESP-BLE-MESH is initialised. Must not block.
 *
 * @param dev_index  Index of the device in the active config, from 0; its
 *                   element is element 1 + @p dev_index.
 * @param cfg        The device's config entry; never NULL.
 * @param sig_models Receives the device's SIG model array, which must stay
 *                   valid for as long as the node runs. Never NULL.
 * @param sig_count  Receives the number of models in @p sig_models. Never
 *                   NULL.
 * @return NODE_MESH_OK, or an error; the element then has no models, and
 *         the failure is logged.
 */
typedef node_mesh_err_t (*node_mesh_models_builder_t)(
    int dev_index, const node_config_dev_t *cfg,
    esp_ble_mesh_model_t **sig_models, uint8_t *sig_count);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Start Bluetooth and the mesh node.
 *
 * Call once, from the boot task, after node_health_init() has initialised
 * NVS. Starts the Bluetooth controller in BLE-only mode and the Bluedroid
 * host, builds the composition, initialises ESP-BLE-MESH, which restores a
 * provisioned node's address and keys, and, if the node is unprovisioned,
 * starts sending unprovisioned beacons on PB-ADV. Also registers the
 * factory-reset hook and logs the mesh state. Returns within 2 s.
 *
 * In normal mode, if the composition changed since provisioning, only the
 * primary element is registered and a warning is logged. In safe mode only
 * the primary element is registered.
 *
 * @param cfg Active config, or NULL in safe mode. Read during the call only.
 * @return NODE_MESH_OK, NODE_MESH_ERR_ARG for a config whose device count is
 *         out of range, NODE_MESH_ERR_STATE if already started,
 *         NODE_MESH_ERR_BT or NODE_MESH_ERR_MESH.
 */
node_mesh_err_t node_mesh_start(const node_config_t *cfg);

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
 * @brief Element of one configured device, for spec 11.
 *
 * Task context, after node_mesh_start().
 *
 * @param dev_index Index of the device in the active config, from 0.
 * @return Element 1 + @p dev_index, or NULL if that device has no element:
 *         out of range, safe mode, a changed composition, or the mesh is not
 *         started.
 */
esp_ble_mesh_elem_t *node_mesh_device_element(int dev_index);

/**
 * @brief Register the builder that gives each device element its models.
 *
 * Call before node_mesh_start(), from task context. A second call replaces
 * the builder.
 *
 * @param builder Models builder; must not be NULL.
 * @return NODE_MESH_OK, NODE_MESH_ERR_ARG for a NULL @p builder, or
 *         NODE_MESH_ERR_STATE if the mesh is already started.
 */
node_mesh_err_t node_mesh_set_models_builder(
    node_mesh_models_builder_t builder);

#ifdef __cplusplus
}
#endif
