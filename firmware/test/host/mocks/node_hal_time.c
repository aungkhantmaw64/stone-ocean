/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_hal_mock.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Microseconds per millisecond. */
#define NODE_HAL_TIME_US_PER_MS 1000U

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Virtual time since the last reset, in ms; wraps around like the
 *         real clock. */
static uint32_t g_now_ms = 0;

/** @brief Busy-waited time not yet added to g_now_ms, in µs; always below
 *         NODE_HAL_TIME_US_PER_MS. */
static uint32_t g_busy_carry_us = 0;

/** @brief Total time busy-waited since the last reset, in µs. */
static uint64_t g_busy_total_us = 0;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_hal_time_mock_reset(void) {
  g_now_ms = 0;
  g_busy_carry_us = 0;
  g_busy_total_us = 0;
}

void node_hal_time_mock_advance(uint32_t ms) { g_now_ms += ms; }

void node_hal_time_mock_set(uint32_t ms) { g_now_ms = ms; }

uint64_t node_hal_time_mock_busy_wait_total_us(void) { return g_busy_total_us; }

uint32_t node_hal_time_ms(void) { return g_now_ms; }

void node_hal_time_delay_ms(uint32_t ms) { g_now_ms += ms; }

node_hal_err_t node_hal_time_busy_wait_us(uint32_t us) {
  if (us > NODE_HAL_TIME_BUSY_WAIT_MAX_US) {
    return NODE_HAL_ERR_ARG;
  }
  g_busy_total_us += us;
  g_busy_carry_us += us;
  g_now_ms += g_busy_carry_us / NODE_HAL_TIME_US_PER_MS;
  g_busy_carry_us %= NODE_HAL_TIME_US_PER_MS;
  return NODE_HAL_OK;
}
