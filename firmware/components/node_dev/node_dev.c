/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev.h"

#include <stdbool.h>

#include "node_ao.h"
#include "node_config.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

_Static_assert(sizeof(node_dev_evt_t) <= NODE_AO_MAX_EVT_SIZE,
               "node_dev_evt_t must fit in an AO queue slot");

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Whether a device index can name a device of any config.
 *
 * @param dev_index Index to check.
 * @return True if @p dev_index is 0 to NODE_CONFIG_MAX_DEVICES - 1.
 */
static bool node_dev_index_in_range(int dev_index);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_dev_err_t node_dev_register_driver(const node_dev_driver_t *drv) {
  if ((drv == NULL) || (drv->init == NULL)) {
    return NODE_DEV_ERR_ARG;
  }
  /* TODO(chunk 5): driver registry. */
  return NODE_DEV_ERR_STATE;
}

node_dev_err_t node_dev_start(const node_config_t *cfg) {
  if (cfg == NULL) {
    return NODE_DEV_ERR_ARG;
  }
  /* TODO(chunk 5): start the devices AO. */
  return NODE_DEV_ERR_STATE;
}

node_dev_err_t node_dev_set_onoff(int dev_index, bool on) {
  (void)on;
  if (!node_dev_index_in_range(dev_index)) {
    return NODE_DEV_ERR_ARG;
  }
  /* TODO(chunk 8): post the command to the devices AO. */
  return NODE_DEV_ERR_STATE;
}

node_dev_err_t node_dev_post_from_isr(int dev_index) {
  if (!node_dev_index_in_range(dev_index)) {
    return NODE_DEV_ERR_ARG;
  }
  /* TODO(chunk 8): post the ISR event to the devices AO. */
  return NODE_DEV_ERR_STATE;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_dev_index_in_range(int dev_index) {
  return (dev_index >= 0) && (dev_index < NODE_CONFIG_MAX_DEVICES);
}
