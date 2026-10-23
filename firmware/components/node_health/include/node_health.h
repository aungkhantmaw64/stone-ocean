#pragma once

/**
 * @file node_health.h
 * @brief Reset reason, crash counter, factory reset and the reset button
 *        (spec 09).
 *
 * node_health_init() runs first in the boot sequence. It initialises NVS,
 * logs why the node reset and how many times it crashed, and starts the
 * health AO, which watches the reset button and factory-resets the node on a
 * long press. Other modules, such as the mesh (spec 10), register hooks that
 * erase their own state during a factory reset.
 *
 * The task watchdog itself is fed by the node_ao port, not through this
 * header. The header must not include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Most factory-reset hooks that can be registered. */
#define NODE_HEALTH_MAX_RESET_HOOKS 4

/** @brief How long the reset button must be held to factory-reset the node,
 *         in ms (spec 09). */
#define NODE_HEALTH_RESET_HOLD_MS 5000

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a node_health call. */
typedef enum {
  NODE_HEALTH_OK = 0,    /**< Success. */
  NODE_HEALTH_ERR_ARG,   /**< Bad argument from the caller, such as a NULL
                            hook. */
  NODE_HEALTH_ERR_FULL,  /**< NODE_HEALTH_MAX_RESET_HOOKS hooks are already
                            registered. */
  NODE_HEALTH_ERR_STATE, /**< node_health_init() has not run, or has already
                            run. */
  NODE_HEALTH_ERR_IO,    /**< NVS could not be read or written. */
} node_health_err_t;

/**
 * @brief Erases one module's state during a factory reset.
 *
 * Runs in the task that called node_health_factory_reset(), before the
 * config is erased and the node restarts. It may block on flash, but must
 * return and must not restart the node itself.
 */
typedef void (*node_health_reset_hook_t)(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Initialise NVS, record the reset and start the health AO.
 *
 * Call once, from task context, first in the boot sequence and before any
 * other module uses NVS. If NVS reports no free pages or a newer format
 * version, logs at ERROR, erases the `nvs` partition and retries. Reads the
 * reset reason, increments the crash counter in NVS if the reason is a
 * crash (panic, task watchdog, interrupt watchdog or brownout), and logs
 * `reset_reason=<reason> crash_count=<n>` once. Then starts the health AO.
 * Failures are logged; the boot goes on.
 */
void node_health_init(void);

/**
 * @brief Register a hook that node_health_factory_reset() runs.
 *
 * Task context only, from one task at a time. Hooks run in the order they
 * were registered. Registering the same hook twice runs it twice.
 *
 * @param hook Hook to add; must not be NULL.
 * @return NODE_HEALTH_OK, NODE_HEALTH_ERR_ARG for a NULL @p hook, or
 *         NODE_HEALTH_ERR_FULL if NODE_HEALTH_MAX_RESET_HOOKS hooks are
 *         already registered.
 */
node_health_err_t node_health_register_reset_hook(
    node_health_reset_hook_t hook);

/**
 * @brief Wipe the node back to factory state and restart it. Does not
 *        return.
 *
 * Logs `FACTORY RESET`, runs the registered hooks, erases the stored config
 * and the crash counter, then restarts the node, which boots unprovisioned
 * in safe mode. An erase that fails is logged and the reset goes on. Task
 * context only; blocks on flash.
 */
void node_health_factory_reset(void);

/**
 * @brief Why the node last reset, as logged at boot.
 *
 * Safe from any task.
 *
 * @return A NUL-terminated lowercase name such as "poweron", "panic" or
 *         "task_wdt"; "unknown" before node_health_init() has run.
 */
const char *node_health_reset_reason_str(void);

/**
 * @brief Number of crash resets since the counter was last cleared.
 *
 * Safe from any task.
 *
 * @return The count as logged at boot, including this boot's reset; 0
 *         before node_health_init() has run or after the counter was
 *         cleared.
 */
uint32_t node_health_crash_count(void);

/**
 * @brief Reset the crash counter to 0, in NVS and in memory.
 *
 * Task context only.
 *
 * @return NODE_HEALTH_OK, NODE_HEALTH_ERR_STATE before node_health_init(),
 *         or NODE_HEALTH_ERR_IO if NVS could not be written; the count in
 *         memory is then left unchanged.
 */
node_health_err_t node_health_crash_count_clear(void);

#ifdef __cplusplus
}
#endif
