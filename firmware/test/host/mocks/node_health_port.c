/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_health_port.h"
#include "node_health_port_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Reason node_health_port_reset_reason() reports. */
static node_health_port_reason_t g_reason = NODE_HEALTH_PORT_REASON_UNKNOWN;

/** @brief Outcome of NVS initialisation until the partition is erased. */
static node_health_port_nvs_t g_nvs_before_erase = NODE_HEALTH_PORT_NVS_OK;

/** @brief Outcome of NVS initialisation once the partition is erased. */
static node_health_port_nvs_t g_nvs_after_erase = NODE_HEALTH_PORT_NVS_OK;

/** @brief True if erasing the NVS partition fails. */
static bool g_fail_nvs_erase = false;

/** @brief Times the NVS partition was erased since the last reset. */
static size_t g_nvs_erase_count = 0;

/** @brief True if a crash counter is stored. */
static bool g_counter_stored = false;

/** @brief The stored crash counter; valid while g_counter_stored. */
static uint32_t g_counter = 0;

/** @brief True if counter reads fail. */
static bool g_fail_load = false;

/** @brief True if counter writes fail. */
static bool g_fail_store = false;

/** @brief True if erasing the `health` namespace fails. */
static bool g_fail_health_erase = false;

/** @brief Times the node was restarted since the last reset. */
static size_t g_restart_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_health_port_mock_reset(void) {
  g_reason = NODE_HEALTH_PORT_REASON_UNKNOWN;
  g_nvs_before_erase = NODE_HEALTH_PORT_NVS_OK;
  g_nvs_after_erase = NODE_HEALTH_PORT_NVS_OK;
  g_fail_nvs_erase = false;
  g_nvs_erase_count = 0;
  g_counter_stored = false;
  g_counter = 0;
  g_fail_load = false;
  g_fail_store = false;
  g_fail_health_erase = false;
  g_restart_count = 0;
}

void node_health_port_mock_set_reset_reason(node_health_port_reason_t reason) {
  g_reason = reason;
}

void node_health_port_mock_set_nvs_init(node_health_port_nvs_t before_erase,
                                        node_health_port_nvs_t after_erase) {
  g_nvs_before_erase = before_erase;
  g_nvs_after_erase = after_erase;
}

void node_health_port_mock_fail_nvs_erase(void) { g_fail_nvs_erase = true; }

size_t node_health_port_mock_nvs_erase_count(void) { return g_nvs_erase_count; }

void node_health_port_mock_set_counter(uint32_t count) {
  g_counter_stored = true;
  g_counter = count;
}

bool node_health_port_mock_counter_is_stored(void) { return g_counter_stored; }

uint32_t node_health_port_mock_counter(void) {
  return g_counter_stored ? g_counter : 0;
}

void node_health_port_mock_fail_counter_load(void) { g_fail_load = true; }

void node_health_port_mock_fail_counter_store(void) { g_fail_store = true; }

void node_health_port_mock_fail_health_erase(void) {
  g_fail_health_erase = true;
}

size_t node_health_port_mock_restart_count(void) { return g_restart_count; }

node_health_port_nvs_t node_health_port_nvs_init(void) {
  return (g_nvs_erase_count > 0) ? g_nvs_after_erase : g_nvs_before_erase;
}

node_health_err_t node_health_port_nvs_erase(void) {
  if (g_fail_nvs_erase) {
    return NODE_HEALTH_ERR_IO;
  }
  g_nvs_erase_count++;
  g_counter_stored = false;
  g_counter = 0;
  return NODE_HEALTH_OK;
}

node_health_port_reason_t node_health_port_reset_reason(void) {
  return g_reason;
}

node_health_err_t node_health_port_counter_load(uint32_t *out) {
  if (out == NULL) {
    return NODE_HEALTH_ERR_ARG;
  }
  *out = 0;
  if (g_fail_load) {
    return NODE_HEALTH_ERR_IO;
  }
  *out = node_health_port_mock_counter();
  return NODE_HEALTH_OK;
}

node_health_err_t node_health_port_counter_store(uint32_t count) {
  if (g_fail_store) {
    return NODE_HEALTH_ERR_IO;
  }
  node_health_port_mock_set_counter(count);
  return NODE_HEALTH_OK;
}

node_health_err_t node_health_port_health_erase(void) {
  if (g_fail_health_erase) {
    return NODE_HEALTH_ERR_IO;
  }
  g_counter_stored = false;
  g_counter = 0;
  return NODE_HEALTH_OK;
}

void node_health_port_restart(void) { g_restart_count++; }
