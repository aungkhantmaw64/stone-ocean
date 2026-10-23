#pragma once

/**
 * @file node_mesh_comp.h
 * @brief Composition plan and device UUID of the mesh node. Not part of the
 *        public interface.
 *
 * Pure logic, built on the target and on the host: decides, before
 * ESP-BLE-MESH starts, which elements the node registers and which
 * composition hash it stores at provisioning (spec 10). The header must not
 * include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "node_config.h"
#include "node_mesh_err.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Most elements the node registers: the primary element plus one per
 *         device. */
#define NODE_MESH_COMP_MAX_ELEMENTS (1 + NODE_CONFIG_MAX_DEVICES)

/** @brief Length of the mesh device UUID, in bytes. */
#define NODE_MESH_COMP_UUID_LEN 16

/** @brief Length of the Bluetooth MAC address, in bytes. */
#define NODE_MESH_COMP_MAC_LEN 6

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief How the composition was decided at boot. */
typedef enum {
  NODE_MESH_COMP_UNPROVISIONED, /**< No hash stored: every device gets an
                                   element. */
  NODE_MESH_COMP_MATCH,         /**< The stored hash matches the config:
                                   every device gets an element. */
  NODE_MESH_COMP_MISMATCH,      /**< The stored hash differs from the
                                   config's: primary element only, until the
                                   node is reprovisioned. */
  NODE_MESH_COMP_SAFE_MODE,     /**< No config: primary element only; the
                                   stored hash is not compared. */
} node_mesh_comp_outcome_t;

/** @brief The composition the node registers this boot. */
typedef struct {
  node_mesh_comp_outcome_t outcome; /**< How it was decided. */
  uint8_t device_count;             /**< Devices that get an element, 0 to
                                       NODE_CONFIG_MAX_DEVICES; device i gets element
                                       1 + i. */
  uint8_t element_count;            /**< Elements registered, the primary one
                                       included: 1 + device_count. */
  uint32_t config_hash; /**< Composition hash of the active config; 0 in
                           safe mode. */
  uint32_t hash_in_use; /**< Composition hash of the elements registered,
                           stored at provisioning: config_hash when the
                           devices have elements, otherwise the hash of a
                           config with no devices. */
} node_mesh_comp_plan_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Decide the composition to register this boot.
 *
 * With no stored hash, or a stored hash equal to the config's, every device
 * gets an element. With a different stored hash, only the primary element
 * is registered. In safe mode the stored hash is ignored.
 *
 * @param cfg         Active config, or NULL in safe mode.
 * @param hash_stored True if a composition hash is stored, that is, the node
 *                    was provisioned.
 * @param stored_hash The stored hash; ignored if @p hash_stored is false.
 * @param out         Receives the plan; zeroed on failure. Must not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL @p out or a config
 *         whose device count is out of range.
 */
node_mesh_err_t node_mesh_comp_plan(const node_config_t *cfg, bool hash_stored,
                                    uint32_t stored_hash,
                                    node_mesh_comp_plan_t *out);

/**
 * @brief Build the mesh device UUID: the ASCII prefix "stoneocean" followed
 *        by the Bluetooth MAC address.
 *
 * @param mac  Bluetooth MAC address, NODE_MESH_COMP_MAC_LEN bytes; must not
 *             be NULL.
 * @param uuid Receives NODE_MESH_COMP_UUID_LEN bytes; must not be NULL.
 * @return NODE_MESH_OK, or NODE_MESH_ERR_ARG for a NULL pointer.
 */
node_mesh_err_t node_mesh_comp_uuid(const uint8_t *mac, uint8_t *uuid);
