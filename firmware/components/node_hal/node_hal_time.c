/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_hal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Microseconds per millisecond. */
#define US_PER_MS 1000

/** @brief Milliseconds per second. */
#define MS_PER_S 1000

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

/* vTaskDelay(n) can return up to one tick early because the current tick is
 * already partly over, so round up and add one tick. A delay too long for
 * TickType_t saturates instead of wrapping to a short one. */
static TickType_t node_hal_time_ticks_for_at_least_ms(uint32_t ms) {
  uint64_t ticks =
      ((uint64_t)ms * configTICK_RATE_HZ + MS_PER_S - 1) / MS_PER_S + 1;
  if (ticks > (uint64_t)portMAX_DELAY) {
    return portMAX_DELAY;
  }
  return (TickType_t)ticks;
}

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

uint32_t node_hal_time_ms(void) {
  return (uint32_t)(esp_timer_get_time() / US_PER_MS);
}

void node_hal_time_delay_ms(uint32_t ms) {
  if (ms == 0) {
    return;
  }
  vTaskDelay(node_hal_time_ticks_for_at_least_ms(ms));
}

node_hal_err_t node_hal_time_busy_wait_us(uint32_t us) {
  if (us > NODE_HAL_TIME_BUSY_WAIT_MAX_US) {
    return NODE_HAL_ERR_ARG;
  }
  esp_rom_delay_us(us);
  return NODE_HAL_OK;
}
