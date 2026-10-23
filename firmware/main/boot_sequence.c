/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "boot_sequence.h"

#include "esp_log.h"

#include "config_store_mode.h"
#include "node_dev.h"
#include "node_dev_led.h"
#include "node_dev_pir.h"
#include "node_hal_selftest.h"
#include "node_status.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief On time of the status LED's heartbeat, in ms (spec 16). */
#define BOOT_SEQUENCE_HEARTBEAT_ON_MS 1000

/** @brief Off time of the status LED's heartbeat, in ms (spec 16). */
#define BOOT_SEQUENCE_HEARTBEAT_OFF_MS 1000

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Log tag of the boot sequence. */
static const char *const TAG = "boot_sequence";

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Start the status AO and its heartbeat, which blinks for as long as
 *        the app runs, in normal and safe mode. A failure is logged; the boot
 *        goes on.
 */
static void boot_sequence_start_status(void);

/**
 * @brief Start the devices AO with the active config, in normal mode only.
 *
 * In safe mode no drivers start. A failure to start is logged.
 */
static void boot_sequence_start_devices(void);

/**
 * @brief Register every device driver with the devices AO.
 *
 * A driver that fails to register is logged; its devices then go to FAULT
 * when the devices AO starts.
 */
static void boot_sequence_register_drivers(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void boot_sequence_run(void) {
  boot_sequence_start_status();
  config_store_mode_boot();
  boot_sequence_start_devices();
  node_hal_selftest_start();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void boot_sequence_start_status(void) {
  node_status_err_t err = node_status_start();
  if (err != NODE_STATUS_OK) {
    ESP_LOGE(TAG, "could not start the status LED: %d", (int)err);
    return;
  }
  err = node_status_led_blink(BOOT_SEQUENCE_HEARTBEAT_ON_MS,
                              BOOT_SEQUENCE_HEARTBEAT_OFF_MS);
  if (err != NODE_STATUS_OK) {
    ESP_LOGE(TAG, "could not start the status LED heartbeat: %d", (int)err);
  }
}

static void boot_sequence_start_devices(void) {
  if (config_store_mode_get() != CONFIG_STORE_MODE_NORMAL) {
    return;
  }
  boot_sequence_register_drivers();
  const node_dev_err_t err = node_dev_start(config_store_mode_active_config());
  if (err != NODE_DEV_OK) {
    ESP_LOGE(TAG, "could not start the devices: %d", (int)err);
  }
}

static void boot_sequence_register_drivers(void) {
  node_dev_err_t err = node_dev_pir_register();
  if (err != NODE_DEV_OK) {
    ESP_LOGE(TAG, "could not register the PIR driver: %d", (int)err);
  }
  err = node_dev_led_register();
  if (err != NODE_DEV_OK) {
    ESP_LOGE(TAG, "could not register the LED driver: %d", (int)err);
  }
}
