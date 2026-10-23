/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "node_health_log.h"
#include "node_health_log_mock.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Size of the buffer that holds the last boot line's reason,
 *         including the terminating NUL. */
#define NODE_HEALTH_LOG_MOCK_REASON_LEN 16

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief "Erasing the nvs partition" lines logged since the last reset. */
static size_t g_nvs_erasing_count = 0;

/** @brief "nvs init failed" lines logged since the last reset. */
static size_t g_nvs_failed_count = 0;

/** @brief Crash counter failures logged since the last reset. */
static size_t g_counter_failed_count = 0;

/** @brief Boot lines logged since the last reset. */
static size_t g_boot_count = 0;

/** @brief Reset reason of the last boot line; "" if none. */
static char g_last_boot_reason[NODE_HEALTH_LOG_MOCK_REASON_LEN] = {0};

/** @brief Crash count of the last boot line; 0 if none. */
static uint32_t g_last_boot_crash_count = 0;

/** @brief `FACTORY RESET` lines logged since the last reset. */
static size_t g_factory_reset_count = 0;

/** @brief Failed factory reset steps logged since the last reset. */
static size_t g_factory_reset_failed_count = 0;

/** @brief "Health AO did not start" lines logged since the last reset. */
static size_t g_ao_start_failed_count = 0;

/** @brief "Reset button watched" lines logged since the last reset. */
static size_t g_button_started_count = 0;

/** @brief Button pin setup failures logged since the last reset. */
static size_t g_button_setup_failed_count = 0;

/** @brief "Reset button held" lines logged since the last reset. */
static size_t g_button_held_count = 0;

/** @brief "Released early" lines logged since the last reset. */
static size_t g_button_released_early_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_health_log_mock_reset(void) {
  g_nvs_erasing_count = 0;
  g_nvs_failed_count = 0;
  g_counter_failed_count = 0;
  g_boot_count = 0;
  g_last_boot_reason[0] = '\0';
  g_last_boot_crash_count = 0;
  g_factory_reset_count = 0;
  g_factory_reset_failed_count = 0;
  g_ao_start_failed_count = 0;
  g_button_started_count = 0;
  g_button_setup_failed_count = 0;
  g_button_held_count = 0;
  g_button_released_early_count = 0;
}

size_t node_health_log_mock_nvs_erasing_count(void) {
  return g_nvs_erasing_count;
}

size_t node_health_log_mock_nvs_failed_count(void) {
  return g_nvs_failed_count;
}

size_t node_health_log_mock_counter_failed_count(void) {
  return g_counter_failed_count;
}

size_t node_health_log_mock_boot_count(void) { return g_boot_count; }

const char *node_health_log_mock_last_boot_reason(void) {
  return g_last_boot_reason;
}

uint32_t node_health_log_mock_last_boot_crash_count(void) {
  return g_last_boot_crash_count;
}

size_t node_health_log_mock_factory_reset_count(void) {
  return g_factory_reset_count;
}

size_t node_health_log_mock_factory_reset_failed_count(void) {
  return g_factory_reset_failed_count;
}

size_t node_health_log_mock_ao_start_failed_count(void) {
  return g_ao_start_failed_count;
}

size_t node_health_log_mock_button_started_count(void) {
  return g_button_started_count;
}

size_t node_health_log_mock_button_setup_failed_count(void) {
  return g_button_setup_failed_count;
}

size_t node_health_log_mock_button_held_count(void) {
  return g_button_held_count;
}

size_t node_health_log_mock_button_released_early_count(void) {
  return g_button_released_early_count;
}

void node_health_log_nvs_erasing(const char *why) {
  (void)why;
  g_nvs_erasing_count++;
}

void node_health_log_nvs_failed(void) { g_nvs_failed_count++; }

void node_health_log_counter_failed(const char *action) {
  (void)action;
  g_counter_failed_count++;
}

void node_health_log_boot(const char *reason, uint32_t crash_count) {
  g_boot_count++;
  (void)snprintf(g_last_boot_reason, sizeof(g_last_boot_reason), "%s",
                 (reason != NULL) ? reason : "");
  g_last_boot_crash_count = crash_count;
}

void node_health_log_factory_reset(void) { g_factory_reset_count++; }

void node_health_log_factory_reset_failed(const char *what) {
  (void)what;
  g_factory_reset_failed_count++;
}

void node_health_log_ao_start_failed(void) { g_ao_start_failed_count++; }

void node_health_log_button_started(int gpio) {
  (void)gpio;
  g_button_started_count++;
}

void node_health_log_button_setup_failed(int gpio, node_hal_err_t err) {
  (void)gpio;
  (void)err;
  g_button_setup_failed_count++;
}

void node_health_log_button_held(uint32_t hold_ms) {
  (void)hold_ms;
  g_button_held_count++;
}

void node_health_log_button_released_early(void) {
  g_button_released_early_count++;
}
