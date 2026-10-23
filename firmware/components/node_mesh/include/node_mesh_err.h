#pragma once

/**
 * @file node_mesh_err.h
 * @brief Result codes of the node_mesh component (spec 10).
 *
 * Kept apart from node_mesh.h, which needs the ESP-BLE-MESH headers, so the
 * component's pure logic and its host tests can use the codes too. The
 * header must not include ESP-IDF headers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a node_mesh call. */
typedef enum {
  NODE_MESH_OK = 0,    /**< Success. */
  NODE_MESH_ERR_ARG,   /**< Bad argument from the caller, such as a NULL
                          pointer or a device count out of range. */
  NODE_MESH_ERR_STATE, /**< The mesh is already started, or not started. */
  NODE_MESH_ERR_BT,    /**< The Bluetooth controller or host failed to
                          start. */
  NODE_MESH_ERR_MESH,  /**< ESP-BLE-MESH init, or enabling provisioning,
                          failed. */
} node_mesh_err_t;

#ifdef __cplusplus
}
#endif
