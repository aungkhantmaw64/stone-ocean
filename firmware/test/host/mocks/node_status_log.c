/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_status_log.h"
#include "node_status_log_mock.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief "Started" lines logged since the last reset. */
static size_t g_started_count = 0;

/** @brief Pin setup failures logged since the last reset. */
static size_t g_setup_failed_count = 0;

/** @brief Steady mode changes logged since the last reset. */
static size_t g_steady_count = 0;

/** @brief Last steady mode change logged: true for on. */
static bool g_last_steady_on = false;

/** @brief Blink mode changes logged since the last reset. */
static size_t g_blink_count = 0;

/** @brief On time of the last blink mode change logged, in ms. */
static uint16_t g_last_blink_on_ms = 0;

/** @brief Off time of the last blink mode change logged, in ms. */
static uint16_t g_last_blink_off_ms = 0;

/** @brief "Writes start failing" lines logged since the last reset. */
static size_t g_write_failed_count = 0;

/** @brief "Writes work again" lines logged since the last reset. */
static size_t g_write_recovered_count = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_status_log_started(int gpio) {
  (void)gpio;
  g_started_count++;
}

void node_status_log_setup_failed(int gpio, node_hal_err_t err) {
  (void)gpio;
  (void)err;
  g_setup_failed_count++;
}

void node_status_log_steady(bool on) {
  g_steady_count++;
  g_last_steady_on = on;
}

void node_status_log_blink(uint16_t on_ms, uint16_t off_ms) {
  g_blink_count++;
  g_last_blink_on_ms = on_ms;
  g_last_blink_off_ms = off_ms;
}

void node_status_log_write_failed(int gpio, node_hal_err_t err) {
  (void)gpio;
  (void)err;
  g_write_failed_count++;
}

void node_status_log_write_recovered(int gpio) {
  (void)gpio;
  g_write_recovered_count++;
}

void node_status_log_mock_reset(void) {
  g_started_count = 0;
  g_setup_failed_count = 0;
  g_steady_count = 0;
  g_last_steady_on = false;
  g_blink_count = 0;
  g_last_blink_on_ms = 0;
  g_last_blink_off_ms = 0;
  g_write_failed_count = 0;
  g_write_recovered_count = 0;
}

size_t node_status_log_mock_started_count(void) { return g_started_count; }

size_t node_status_log_mock_setup_failed_count(void) {
  return g_setup_failed_count;
}

size_t node_status_log_mock_steady_count(void) { return g_steady_count; }

bool node_status_log_mock_last_steady_on(void) { return g_last_steady_on; }

size_t node_status_log_mock_blink_count(void) { return g_blink_count; }

uint16_t node_status_log_mock_last_blink_on_ms(void) {
  return g_last_blink_on_ms;
}

uint16_t node_status_log_mock_last_blink_off_ms(void) {
  return g_last_blink_off_ms;
}

size_t node_status_log_mock_write_failed_count(void) {
  return g_write_failed_count;
}

size_t node_status_log_mock_write_recovered_count(void) {
  return g_write_recovered_count;
}
