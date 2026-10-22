/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "boot_sequence.h"

#include "esp_log.h"

#include "config_store_mode.h"
#include "node_dev.h"
#include "node_hal_selftest.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Log tag of the boot sequence. */
static const char *const TAG = "boot_sequence";

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Start the devices AO with the active config, in normal mode only.
 *
 * In safe mode no drivers start. A failure to start is logged.
 */
static void boot_sequence_start_devices(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void boot_sequence_run(void) {
  config_store_mode_boot();
  boot_sequence_start_devices();
  node_hal_selftest_start();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void boot_sequence_start_devices(void) {
  if (config_store_mode_get() != CONFIG_STORE_MODE_NORMAL) {
    return;
  }
  const node_dev_err_t err = node_dev_start(config_store_mode_active_config());
  if (err != NODE_DEV_OK) {
    ESP_LOGE(TAG, "could not start the devices: %d", (int)err);
  }
}
