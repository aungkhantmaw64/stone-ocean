#pragma once

/**
 * @file node_mesh_log.h
 * @brief Log lines of node_mesh. Not part of the public interface.
 *
 * Logs with ESP_LOG under the tag "node_mesh" on the target. The host mock
 * records what was logged so tests can check it. The header must not include
 * ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Log at INFO level that provisioning completed: `mesh=prov
 *        addr=<addr> elements=<n> relay=<on|off>`, then the network key
 *        index and IV index.
 *
 * @param addr          Unicast address of the primary element.
 * @param element_count Elements registered.
 * @param relay         True if the relay feature is enabled.
 * @param net_idx       Index of the network key.
 * @param iv_index      IV index of the network.
 */
void node_mesh_log_prov_complete(uint16_t addr, uint8_t element_count,
                                 bool relay, uint16_t net_idx,
                                 uint32_t iv_index);

/**
 * @brief Log at ERROR level that the composition hash could not be stored,
 *        so a later config change will not be detected.
 */
void node_mesh_log_hash_store_failed(void);

/**
 * @brief Log at WARNING level that the gateway reset the node, which now
 *        erases its mesh metadata and restarts.
 */
void node_mesh_log_node_reset(void);

/**
 * @brief Log at ERROR level that the mesh metadata could not be erased.
 */
void node_mesh_log_meta_erase_failed(void);

/**
 * @brief Log at ERROR level that a mesh event could not be posted to the
 *        mesh AO and is lost.
 *
 * @param what NUL-terminated name of the event, such as "prov complete".
 */
void node_mesh_log_post_failed(const char *what);
