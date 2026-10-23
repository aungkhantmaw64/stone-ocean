/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_mesh_uuid.h"

#include <stdint.h>
#include <string.h>

#include "node_mesh_err.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Fixed start of every device UUID (spec 10); not NUL-terminated in
 *         the UUID. */
#define NODE_MESH_UUID_PREFIX "stoneocean"

/** @brief Length of NODE_MESH_UUID_PREFIX, without its NUL. */
#define NODE_MESH_UUID_PREFIX_LEN (sizeof(NODE_MESH_UUID_PREFIX) - 1u)

_Static_assert(NODE_MESH_UUID_PREFIX_LEN + NODE_MESH_UUID_MAC_LEN ==
                   NODE_MESH_UUID_LEN,
               "the UUID is the prefix followed by the MAC address");

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_mesh_err_t node_mesh_uuid_build(const uint8_t *mac, uint8_t *uuid) {
  if ((mac == NULL) || (uuid == NULL)) {
    return NODE_MESH_ERR_ARG;
  }
  memcpy(uuid, NODE_MESH_UUID_PREFIX, NODE_MESH_UUID_PREFIX_LEN);
  memcpy(&uuid[NODE_MESH_UUID_PREFIX_LEN], mac, NODE_MESH_UUID_MAC_LEN);
  return NODE_MESH_OK;
}
