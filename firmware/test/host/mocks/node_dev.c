/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_dev_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Result of every node_dev_set_onoff(). */
static node_dev_err_t g_onoff_result = NODE_DEV_OK;

/** @brief On/off commands recorded since the last reset. */
static size_t g_onoff_count = 0;

/** @brief Device index of the last command; -1 if none. */
static int g_last_dev = -1;

/** @brief State asked for by the last command; false if none. */
static bool g_last_on = false;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_dev_mock_reset(void) {
  g_onoff_result = NODE_DEV_OK;
  g_onoff_count = 0;
  g_last_dev = -1;
  g_last_on = false;
}

void node_dev_mock_set_onoff_result(node_dev_err_t err) {
  g_onoff_result = err;
}

size_t node_dev_mock_onoff_count(void) { return g_onoff_count; }

int node_dev_mock_last_onoff_dev(void) { return g_last_dev; }

bool node_dev_mock_last_onoff_on(void) { return g_last_on; }

node_dev_err_t node_dev_set_onoff(int dev_index, bool on) {
  if ((dev_index < 0) || (dev_index >= NODE_CONFIG_MAX_DEVICES)) {
    return NODE_DEV_ERR_ARG;
  }
  if (g_onoff_result != NODE_DEV_OK) {
    return g_onoff_result;
  }
  g_onoff_count++;
  g_last_dev = dev_index;
  g_last_on = on;
  return NODE_DEV_OK;
}
