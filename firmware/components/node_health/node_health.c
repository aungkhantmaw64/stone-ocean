/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_health.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_store.h"
#include "node_health_log.h"
#include "node_health_port.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Reset reason reported before node_health_init() has run. */
#define NODE_HEALTH_REASON_NOT_READ "unknown"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief How one reset reason is reported. */
typedef struct {
  const char *name; /**< Lowercase name logged at boot; never NULL. */
  bool is_crash;    /**< True if this reset increments the crash counter. */
} node_health_reason_info_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Name and crash flag of every reset reason, indexed by
 *         node_health_port_reason_t (spec 09). */
static const node_health_reason_info_t
    g_reason_info[NODE_HEALTH_PORT_REASON_COUNT] = {
        [NODE_HEALTH_PORT_REASON_UNKNOWN] = {"unknown", false},
        [NODE_HEALTH_PORT_REASON_POWERON] = {"poweron", false},
        [NODE_HEALTH_PORT_REASON_EXT] = {"ext", false},
        [NODE_HEALTH_PORT_REASON_SW] = {"sw", false},
        [NODE_HEALTH_PORT_REASON_PANIC] = {"panic", true},
        [NODE_HEALTH_PORT_REASON_INT_WDT] = {"int_wdt", true},
        [NODE_HEALTH_PORT_REASON_TASK_WDT] = {"task_wdt", true},
        [NODE_HEALTH_PORT_REASON_WDT] = {"wdt", false},
        [NODE_HEALTH_PORT_REASON_DEEPSLEEP] = {"deepsleep", false},
        [NODE_HEALTH_PORT_REASON_BROWNOUT] = {"brownout", true},
        [NODE_HEALTH_PORT_REASON_SDIO] = {"sdio", false},
};

/** @brief True once node_health_init() has run; written once, from the boot
 *         task. */
static bool g_initialised = false;

/** @brief True if NVS is usable this boot; written by node_health_init(). */
static bool g_nvs_ready = false;

/** @brief Name of this boot's reset reason; written by node_health_init(). */
static const char *g_reason_name = NODE_HEALTH_REASON_NOT_READ;

/** @brief Crash count as logged at boot, or 0 after a clear; written from
 *         task context. */
static uint32_t g_crash_count = 0;

/** @brief Registered factory-reset hooks, in registration order; the first
 *         g_hook_count entries are valid. Written from task context. */
static node_health_reset_hook_t g_hooks[NODE_HEALTH_MAX_RESET_HOOKS] = {0};

/** @brief Number of valid entries in g_hooks, 0 to
 *         NODE_HEALTH_MAX_RESET_HOOKS. */
static size_t g_hook_count = 0;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Initialise NVS, erasing the partition and retrying once if NVS
 *        reports no free pages or a newer format.
 *
 * @return True if NVS is ready.
 */
static bool node_health_nvs_start(void);

/**
 * @brief Look up how a reset reason is reported.
 *
 * @param reason Reason from the port; an out-of-range value is treated as
 *               unknown.
 * @return The reason's entry in g_reason_info; never NULL.
 */
static const node_health_reason_info_t *node_health_reason_info(
    node_health_port_reason_t reason);

/**
 * @brief Read the crash counter, increment it if @p is_crash, and store it
 *        back. Failures are logged.
 *
 * @param is_crash True if this boot's reset was a crash.
 * @return The count to report: the stored one, incremented for a crash and
 *         saturating at UINT32_MAX; 0 if it could not be read.
 */
static uint32_t node_health_count_boot(bool is_crash);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_health_init(void) {
  if (g_initialised) {
    return;
  }
  g_initialised = true;
  g_nvs_ready = node_health_nvs_start();

  const node_health_reason_info_t *const info =
      node_health_reason_info(node_health_port_reset_reason());
  g_reason_name = info->name;
  g_crash_count = g_nvs_ready ? node_health_count_boot(info->is_crash) : 0;
  node_health_log_boot(g_reason_name, g_crash_count);
}

const char *node_health_reset_reason_str(void) { return g_reason_name; }

uint32_t node_health_crash_count(void) { return g_crash_count; }

node_health_err_t node_health_register_reset_hook(
    node_health_reset_hook_t hook) {
  if (hook == NULL) {
    return NODE_HEALTH_ERR_ARG;
  }
  if (g_hook_count >= NODE_HEALTH_MAX_RESET_HOOKS) {
    return NODE_HEALTH_ERR_FULL;
  }
  g_hooks[g_hook_count] = hook;
  g_hook_count++;
  return NODE_HEALTH_OK;
}

void node_health_factory_reset(void) {
  node_health_log_factory_reset();
  for (size_t i = 0; (i < g_hook_count) && (i < NODE_HEALTH_MAX_RESET_HOOKS);
       i++) {
    g_hooks[i]();
  }
  if (config_store_erase() != CONFIG_STORE_OK) {
    node_health_log_factory_reset_failed("config erase");
  }
  if (!g_nvs_ready || (node_health_port_health_erase() != NODE_HEALTH_OK)) {
    node_health_log_factory_reset_failed("crash counter erase");
  }
  node_health_port_restart();
}

node_health_err_t node_health_crash_count_clear(void) {
  if (!g_initialised) {
    return NODE_HEALTH_ERR_STATE;
  }
  if (!g_nvs_ready || (node_health_port_counter_store(0) != NODE_HEALTH_OK)) {
    node_health_log_counter_failed("clear");
    return NODE_HEALTH_ERR_IO;
  }
  g_crash_count = 0;
  return NODE_HEALTH_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_health_nvs_start(void) {
  node_health_port_nvs_t result = node_health_port_nvs_init();
  if ((result == NODE_HEALTH_PORT_NVS_NO_FREE) ||
      (result == NODE_HEALTH_PORT_NVS_NEW_VERSION)) {
    node_health_log_nvs_erasing((result == NODE_HEALTH_PORT_NVS_NO_FREE)
                                    ? "no free pages"
                                    : "newer format version");
    if (node_health_port_nvs_erase() == NODE_HEALTH_OK) {
      result = node_health_port_nvs_init();
    }
  }
  if (result != NODE_HEALTH_PORT_NVS_OK) {
    node_health_log_nvs_failed();
    return false;
  }
  return true;
}

static const node_health_reason_info_t *node_health_reason_info(
    node_health_port_reason_t reason) {
  if (((int)reason < 0) || (reason >= NODE_HEALTH_PORT_REASON_COUNT)) {
    return &g_reason_info[NODE_HEALTH_PORT_REASON_UNKNOWN];
  }
  return &g_reason_info[reason];
}

static uint32_t node_health_count_boot(bool is_crash) {
  uint32_t count = 0;
  if (node_health_port_counter_load(&count) != NODE_HEALTH_OK) {
    node_health_log_counter_failed("read");
    return 0;
  }
  if (!is_crash) {
    return count;
  }
  if (count < UINT32_MAX) {
    count++;
  }
  if (node_health_port_counter_store(count) != NODE_HEALTH_OK) {
    node_health_log_counter_failed("write");
  }
  return count;
}
