#pragma once

/**
 * @file node_mesh_models.h
 * @brief Stone Ocean Node Server vendor model (spec 11): every configured
 *        device on the mesh.
 *
 * The models AO subscribes to the devices AO's published events
 * (NODE_DEV_SIG_*), keeps the latest value and fault state of each device,
 * and publishes a DeviceStatus when one changes and the gateway has set a
 * publish address. ESP-BLE-MESH callbacks only post requests to it.
 *
 * Only built when CONFIG_NODE_MESH_ENABLE is set. The header must not
 * include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_config.h"
#include "node_mesh_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Start the models AO, subscribe it to the NODE_DEV_SIG_* events and
 *        register the vendor model with node_mesh.
 *
 * Call once, from the boot task, before node_dev_start(), so the AO receives
 * each device's first value, and before node_mesh_start(), so the vendor
 * model is part of the composition.
 *
 * @param cfg Active config, or NULL in safe mode (no devices). Must stay
 *            valid while the node runs.
 * @return NODE_MESH_OK, NODE_MESH_ERR_ARG for a config whose device count is
 *         out of range, or NODE_MESH_ERR_STATE if already started, if the AO
 *         or its subscriptions could not be set up, or if the vendor model
 *         could not be registered.
 */
node_mesh_err_t node_mesh_models_start(const node_config_t *cfg);

#ifdef __cplusplus
}
#endif
