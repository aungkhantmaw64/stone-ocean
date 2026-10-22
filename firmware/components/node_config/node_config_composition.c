/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>

#include "node_config.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief 32-bit FNV-1a offset basis. */
#define NODE_CONFIG_COMPOSITION_FNV_OFFSET_BASIS 2166136261u

/** @brief 32-bit FNV-1a prime. */
#define NODE_CONFIG_COMPOSITION_FNV_PRIME 16777619u

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Fold one byte into a 32-bit FNV-1a hash.
 *
 * @param hash Hash so far.
 * @param byte Byte to add.
 * @return The updated hash.
 */
static uint32_t node_config_composition_mix(uint32_t hash, uint8_t byte);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/* FNV-1a over the device count, then one byte per device type. The hash is
 * stored in flash by spec 10, so the byte sequence must stay stable across
 * firmware versions. */
uint32_t node_config_composition_hash(const node_config_t *cfg) {
  if (cfg == NULL || cfg->n_dev < 0 || cfg->n_dev > NODE_CONFIG_MAX_DEVICES) {
    return 0;
  }
  uint32_t hash = NODE_CONFIG_COMPOSITION_FNV_OFFSET_BASIS;
  hash = node_config_composition_mix(hash, (uint8_t)cfg->n_dev);
  for (int i = 0; i < cfg->n_dev; i++) {
    hash = node_config_composition_mix(hash, (uint8_t)cfg->dev[i].type);
  }
  return hash;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static uint32_t node_config_composition_mix(uint32_t hash, uint8_t byte) {
  return (hash ^ byte) * NODE_CONFIG_COMPOSITION_FNV_PRIME;
}
