/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_comp.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "node_config.h"
#include "node_mesh_err.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Fixed start of every device UUID (spec 10); not NUL-terminated in
 *         the UUID. */
#define NODE_MESH_COMP_UUID_PREFIX "stoneocean"

/** @brief Length of NODE_MESH_COMP_UUID_PREFIX, without its NUL. */
#define NODE_MESH_COMP_UUID_PREFIX_LEN (sizeof(NODE_MESH_COMP_UUID_PREFIX) - 1u)

_Static_assert(NODE_MESH_COMP_UUID_PREFIX_LEN + NODE_MESH_COMP_MAC_LEN ==
                   NODE_MESH_COMP_UUID_LEN,
               "the UUID is the prefix followed by the MAC address");

_Static_assert(NODE_MESH_COMP_MAX_ELEMENTS <= UINT8_MAX,
               "element counts are stored in a uint8_t");

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief A config with no devices, whose hash stands for a composition of
 *         the primary element only. Never written. */
static const node_config_t g_no_devices_config = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Fill a plan that registers the primary element only.
 *
 * @param outcome     Why only the primary element is registered.
 * @param config_hash Composition hash of the active config, or 0 in safe
 *                    mode.
 * @param out         Plan to fill; must not be NULL.
 */
static void node_mesh_comp_primary_only(node_mesh_comp_outcome_t outcome,
                                        uint32_t config_hash,
                                        node_mesh_comp_plan_t *out);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_comp_plan(const node_config_t *cfg, bool hash_stored,
                                    uint32_t stored_hash,
                                    node_mesh_comp_plan_t *out) {
  if (out == NULL) {
    return NODE_MESH_ERR_ARG;
  }
  memset(out, 0, sizeof(*out));
  if (cfg == NULL) {
    node_mesh_comp_primary_only(NODE_MESH_COMP_SAFE_MODE, 0, out);
    return NODE_MESH_OK;
  }
  if ((cfg->n_dev < 0) || (cfg->n_dev > NODE_CONFIG_MAX_DEVICES)) {
    return NODE_MESH_ERR_ARG;
  }

  const uint32_t config_hash = node_config_composition_hash(cfg);
  if (hash_stored && (stored_hash != config_hash)) {
    node_mesh_comp_primary_only(NODE_MESH_COMP_MISMATCH, config_hash, out);
    return NODE_MESH_OK;
  }
  out->outcome =
      hash_stored ? NODE_MESH_COMP_MATCH : NODE_MESH_COMP_UNPROVISIONED;
  out->device_count = (uint8_t)cfg->n_dev;
  out->element_count = (uint8_t)(1 + cfg->n_dev);
  out->config_hash = config_hash;
  out->hash_in_use = config_hash;
  return NODE_MESH_OK;
}

node_mesh_err_t node_mesh_comp_uuid(const uint8_t *mac, uint8_t *uuid) {
  if ((mac == NULL) || (uuid == NULL)) {
    return NODE_MESH_ERR_ARG;
  }
  memcpy(uuid, NODE_MESH_COMP_UUID_PREFIX, NODE_MESH_COMP_UUID_PREFIX_LEN);
  memcpy(&uuid[NODE_MESH_COMP_UUID_PREFIX_LEN], mac, NODE_MESH_COMP_MAC_LEN);
  return NODE_MESH_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_mesh_comp_primary_only(node_mesh_comp_outcome_t outcome,
                                        uint32_t config_hash,
                                        node_mesh_comp_plan_t *out) {
  out->outcome = outcome;
  out->device_count = 0;
  out->element_count = 1;
  out->config_hash = config_hash;
  out->hash_in_use = node_config_composition_hash(&g_no_devices_config);
}
