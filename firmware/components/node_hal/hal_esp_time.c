#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_hal.h"

#define US_PER_MS 1000
#define MS_PER_S 1000

uint32_t hal_time_ms(void) {
  return (uint32_t)(esp_timer_get_time() / US_PER_MS);
}

/* vTaskDelay(n) can return up to one tick early because the current tick is
 * already partly over, so round up and add one tick. */
static TickType_t ticks_for_at_least_ms(uint32_t ms) {
  uint64_t ticks =
      ((uint64_t)ms * configTICK_RATE_HZ + MS_PER_S - 1) / MS_PER_S;
  return (TickType_t)(ticks + 1);
}

void hal_delay_ms(uint32_t ms) {
  if (ms == 0) {
    return;
  }
  vTaskDelay(ticks_for_at_least_ms(ms));
}
