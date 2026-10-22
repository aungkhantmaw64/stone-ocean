/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "config_store_mode.h"

#include <stdio.h>

#include "config_store.h"
#include "config_store_fs.h"
#include "config_store_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Size of the safe-mode reason buffer, including the terminating
 *         NUL; fits any node_config_err_t::msg. */
#define CONFIG_STORE_MODE_REASON_LEN NODE_CONFIG_ERR_MSG_LEN

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Mode chosen by the last config_store_mode_boot(); safe until then.
 *         Written only by config_store_mode_boot(). */
static config_store_mode_t g_mode = CONFIG_STORE_MODE_SAFE;

/** @brief Parsed config; valid only in normal mode. Written only by
 *         config_store_mode_boot(). */
static node_config_t g_active_config = {0};

/** @brief Raw JSON read from flash at boot. Static because 4 KB does not fit
 *         the main task's stack. Written only by config_store_mode_boot(). */
static char g_raw_config[NODE_CONFIG_MAX_RAW_LEN] = {0};

/** @brief NUL-terminated safe-mode reason; empty in normal mode. Written only
 *         by config_store_mode_boot(). */
static char g_safe_reason[CONFIG_STORE_MODE_REASON_LEN] = "config not loaded";

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Switch to safe mode, record @p reason and log it once.
 *
 * @param reason NUL-terminated reason; truncated to fit
 *               CONFIG_STORE_MODE_REASON_LEN.
 */
static void config_store_mode_enter_safe(const char *reason);

/**
 * @brief Safe-mode reason for a failed config_store_load().
 *
 * @param err Error returned by config_store_load().
 * @return A NUL-terminated string literal.
 */
static const char *config_store_mode_load_reason(config_store_err_t err);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void config_store_mode_boot(void) {
  if (config_store_fs_init() != CONFIG_STORE_OK) {
    config_store_mode_enter_safe("config storage unavailable");
    return;
  }

  size_t raw_len = 0;
  config_store_err_t load_err =
      config_store_load(g_raw_config, sizeof(g_raw_config), &raw_len);
  if (load_err != CONFIG_STORE_OK) {
    config_store_mode_enter_safe(config_store_mode_load_reason(load_err));
    return;
  }

  node_config_err_t parse_err = {0};
  if (node_config_parse(g_raw_config, raw_len, &g_active_config, &parse_err) !=
      NODE_CONFIG_OK) {
    config_store_mode_enter_safe(
        (parse_err.msg[0] != '\0') ? parse_err.msg : "config is invalid");
    return;
  }

  g_mode = CONFIG_STORE_MODE_NORMAL;
  g_safe_reason[0] = '\0';
  config_store_log_normal_mode(g_active_config.node_name,
                               g_active_config.n_dev);
}

config_store_mode_t config_store_mode_get(void) { return g_mode; }

const node_config_t *config_store_mode_active_config(void) {
  if (g_mode != CONFIG_STORE_MODE_NORMAL) {
    return NULL;
  }
  return &g_active_config;
}

const char *config_store_mode_safe_reason(void) {
  if (g_mode != CONFIG_STORE_MODE_SAFE) {
    return NULL;
  }
  return g_safe_reason;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void config_store_mode_enter_safe(const char *reason) {
  g_mode = CONFIG_STORE_MODE_SAFE;
  /* Every reason fits by construction; truncation would only shorten the
   * log line, so the result is not needed. */
  (void)snprintf(g_safe_reason, sizeof(g_safe_reason), "%s", reason);
  config_store_log_safe_mode(g_safe_reason);
}

static const char *config_store_mode_load_reason(config_store_err_t err) {
  switch (err) {
    case CONFIG_STORE_ERR_NOT_FOUND:
      return "no config stored";
    case CONFIG_STORE_ERR_CORRUPT:
      return "stored config is corrupt";
    case CONFIG_STORE_ERR_TOO_LARGE:
      return "stored config is too large";
    case CONFIG_STORE_ERR_IO:
      return "config storage read failed";
    default:
      return "config storage error";
  }
}
