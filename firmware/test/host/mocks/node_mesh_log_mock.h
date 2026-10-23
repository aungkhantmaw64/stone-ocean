#pragma once

/**
 * @file node_mesh_log_mock.h
 * @brief Test controls for the host implementation of node_mesh_log.c.
 *
 * The mock records the log lines of the node_mesh sources built on the host
 * (the mesh AO) instead of logging them. Lines only
 * node_mesh.c logs are not implemented, as node_mesh.c needs ESP-BLE-MESH
 * and is not built on the host. Call node_mesh_log_mock_reset() from every
 * test's setUp() so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_mesh_err.h"
#include "node_mesh_internal.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Arguments of the last provisioning-complete line. */
typedef struct {
  uint16_t addr;         /**< Unicast address of the primary element. */
  uint8_t element_count; /**< Elements registered. */
  bool relay;            /**< True if relay was logged as on. */
  uint16_t net_idx;      /**< Index of the network key. */
  uint32_t iv_index;     /**< IV index of the network. */
} node_mesh_log_mock_prov_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded log line. */
void node_mesh_log_mock_reset(void);

/**
 * @brief Number of provisioning-complete lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_prov_complete_count(void);

/**
 * @brief Arguments of the last provisioning-complete line.
 *
 * @return The arguments; all zero if none was logged.
 */
node_mesh_log_mock_prov_t node_mesh_log_mock_last_prov_complete(void);

/**
 * @brief Number of "node reset by the gateway" lines logged since the last
 *        reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_node_reset_count(void);

/**
 * @brief Number of "mesh event lost" lines logged since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_post_failed_count(void);

/**
 * @brief Number of provisioning notices logged since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_notice_count(void);

/**
 * @brief The last provisioning notice logged.
 *
 * @return The notice; NODE_MESH_AO_NOTICE_LINK_OPEN if none was logged.
 */
node_mesh_ao_notice_t node_mesh_log_mock_last_notice(void);

/**
 * @brief Error code of the last provisioning notice logged.
 *
 * @return The error; 0 if none was logged.
 */
int32_t node_mesh_log_mock_last_notice_err(void);

/**
 * @brief Number of "device event ignored: no such device" lines logged
 *        since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_unknown_device_count(void);

/**
 * @brief Number of "in FAULT" and "left FAULT" lines logged since the last
 *        reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_fault_count(void);

/**
 * @brief Whether the last FAULT line logged was for entering FAULT.
 *
 * @return True if it was; false if it was for leaving, or none was logged.
 */
bool node_mesh_log_mock_last_fault(void);

/**
 * @brief Number of "device status not published" lines logged since the
 *        last reset.
 *
 * @return The count.
 */
size_t node_mesh_log_mock_publish_failed_count(void);

/**
 * @brief Error of the last "device status not published" line.
 *
 * @return The error; NODE_MESH_OK if none was logged.
 */
node_mesh_err_t node_mesh_log_mock_last_publish_err(void);
