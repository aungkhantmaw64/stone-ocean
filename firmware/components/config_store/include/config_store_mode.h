#pragma once

/**
 * @file config_store_mode.h
 * @brief Boot-time decision between normal mode and safe mode.
 *
 * At boot the stored config is loaded and parsed. A valid config puts the
 * node in normal mode; a missing, corrupt or invalid one puts it in safe
 * mode, where no drivers start but the CLI stays available. The header must
 * not include ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Operating mode chosen at boot. */
typedef enum {
  CONFIG_STORE_MODE_NORMAL, /**< A valid config is active; drivers start. */
  CONFIG_STORE_MODE_SAFE,   /**< No usable config; drivers and device models
                               do not start. */
} config_store_mode_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Load and parse the stored config and choose the mode.
 *
 * Called once from the boot sequence, in task context, after the config
 * filesystem is mounted. In safe mode, logs "SAFE MODE: <reason>" at ERROR
 * level exactly once per call. A later call re-runs the decision.
 */
void config_store_mode_boot(void);

/**
 * @brief Mode chosen by the last config_store_mode_boot().
 *
 * Safe to call from any task once config_store_mode_boot() has returned.
 *
 * @return The mode; CONFIG_STORE_MODE_SAFE before config_store_mode_boot()
 *         has run.
 */
config_store_mode_t config_store_mode_get(void);

/**
 * @brief Parsed config that the node runs with.
 *
 * Safe to call from any task once config_store_mode_boot() has returned. The
 * struct does not change until the next config_store_mode_boot().
 *
 * @return The config in normal mode, or NULL in safe mode.
 */
const node_config_t *config_store_mode_active_config(void);

/**
 * @brief Why the node is in safe mode.
 *
 * Safe to call from any task once config_store_mode_boot() has returned.
 *
 * @return A NUL-terminated, human-readable reason such as "no config stored"
 *         or "devices[1].gpio: pin 7 is reserved for flash"; "config not
 *         loaded" before config_store_mode_boot() has run; NULL in normal
 *         mode.
 */
const char *config_store_mode_safe_reason(void);

#ifdef __cplusplus
}
#endif
