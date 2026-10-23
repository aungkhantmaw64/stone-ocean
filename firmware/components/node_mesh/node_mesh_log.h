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
#include <stddef.h>
#include <stdint.h>

#include "node_mesh_err.h"
#include "node_mesh_internal.h"

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
 * @brief Log at WARNING level that the gateway reset the node, which now
 *        restarts.
 */
void node_mesh_log_node_reset(void);

/**
 * @brief Log at ERROR level that a mesh event could not be posted to the
 *        mesh AO and is lost.
 *
 * @param what NUL-terminated name of the event, such as "prov complete".
 */
void node_mesh_log_post_failed(const char *what);

/**
 * @brief Log at INFO level a provisioning notice from the mesh AO.
 *
 * @param notice What happened.
 * @param err    ESP-BLE-MESH error code, or 0.
 */
void node_mesh_log_notice(node_mesh_ao_notice_t notice, int32_t err);

/**
 * @brief Log at ERROR level that an ESP-IDF call failed.
 *
 * @param what NUL-terminated name of the call, such as "esp_ble_mesh_init".
 * @param err  ESP-IDF error code.
 */
void node_mesh_log_call_failed(const char *what, int32_t err);

/**
 * @brief Log at ERROR level that the factory-reset hook could not be
 *        registered, so a factory reset leaves the node provisioned.
 */
void node_mesh_log_hook_failed(void);

/**
 * @brief Log at INFO level the device UUID, as 32 hex digits.
 *
 * @param uuid UUID bytes; must not be NULL.
 * @param len  Number of bytes in @p uuid.
 */
void node_mesh_log_uuid(const uint8_t *uuid, size_t len);

/**
 * @brief Log at INFO level the mesh state at boot: `mesh=unprov|prov
 *        addr=<addr> elements=<n> relay=<on|off>`.
 *
 * @param provisioned   True if the node is provisioned.
 * @param addr          Unicast address of the primary element; 0 when
 *                      unprovisioned.
 * @param element_count Elements registered.
 * @param relay         True if the relay feature is enabled.
 */
void node_mesh_log_state(bool provisioned, uint16_t addr, uint8_t element_count,
                         bool relay);

/**
 * @brief Log at INFO level the IV index and the network and application key
 *        indexes of a provisioned node.
 *
 * @param iv_index IV index of the network.
 * @param net_idx  Network key indexes; may be NULL if @p n_net is 0.
 * @param n_net    Number of entries in @p net_idx.
 * @param app_idx  Application key indexes; may be NULL if @p n_app is 0.
 * @param n_app    Number of entries in @p app_idx.
 */
void node_mesh_log_keys(uint32_t iv_index, const uint16_t *net_idx,
                        size_t n_net, const uint16_t *app_idx, size_t n_app);

/**
 * @brief Log at INFO level the primary element, the node's only element.
 *
 * @param sig_count Number of SIG models on it.
 * @param vnd_count Number of vendor models on it.
 */
void node_mesh_log_primary_element(uint8_t sig_count, uint8_t vnd_count);

/**
 * @brief Log the free heap after the mesh started, at INFO level, or at
 *        ERROR level if internal RAM is below the spec's 40 KB floor.
 *
 * @param internal Free internal RAM, in bytes.
 * @param total    Free heap of every kind, PSRAM included, in bytes.
 * @param low      True if @p internal is below the floor.
 */
void node_mesh_log_heap(size_t internal, size_t total, bool low);

/**
 * @brief Log at INFO level how long the mesh start took.
 *
 * @param elapsed_ms Time spent in node_mesh_start(), in ms.
 */
void node_mesh_log_started(uint32_t elapsed_ms);

/**
 * @brief Log at ERROR level that the mesh reset of a factory reset did not
 *        finish in time; the factory reset goes on.
 */
void node_mesh_log_reset_timeout(void);

/**
 * @brief Log at WARNING level that the models AO got an event for a device
 *        the config does not have, and ignored it.
 *
 * @param dev_index Device index in the event.
 */
void node_mesh_log_models_unknown_device(int dev_index);

/**
 * @brief Log that a device entered FAULT, at WARNING level, and is reported
 *        on the mesh with unknown values; or that it left FAULT, at INFO
 *        level.
 *
 * @param dev_index Device index.
 * @param fault     True if the device entered FAULT.
 */
void node_mesh_log_models_fault(int dev_index, bool fault);

/**
 * @brief Log at ERROR level that a device's DeviceStatus could not be
 *        published.
 *
 * @param dev_index Device index.
 * @param err       Why: an encoding or ESP-BLE-MESH error.
 */
void node_mesh_log_models_publish_failed(int dev_index, node_mesh_err_t err);

/**
 * @brief Log at WARNING level that a request was dropped without a reply.
 *
 * @param op   Opcode number of the request.
 * @param addr Address of the sender.
 * @param why  Why it was dropped.
 */
void node_mesh_log_models_request_dropped(uint8_t op, uint16_t addr,
                                          node_mesh_models_drop_t why);

/**
 * @brief Log at ERROR level that a reply could not be sent.
 *
 * @param op   Opcode number of the reply.
 * @param addr Address it was for.
 * @param err  Why: an encoding or ESP-BLE-MESH error.
 */
void node_mesh_log_models_send_failed(uint8_t op, uint16_t addr,
                                      node_mesh_err_t err);

/**
 * @brief Log at ERROR level that an OnOffSet could not be passed to the
 *        devices AO, so the LED was not switched.
 *
 * @param dev_index Device index.
 * @param err       node_dev error code.
 */
void node_mesh_log_models_onoff_not_queued(int dev_index, int err);
