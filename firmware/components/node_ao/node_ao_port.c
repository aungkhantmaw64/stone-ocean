/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "node_ao.h"
#include "node_ao_port.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the node_ao FreeRTOS port. */
#define NODE_AO_PORT_LOG_TAG "node_ao"

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Body of every AO thread: subscribe it to the task watchdog, then
 *        run the AO loop forever, feeding the watchdog on every turn.
 *
 * @param arg The AO; never NULL.
 */
static void node_ao_port_thread(void *arg);

/**
 * @brief Convert a timeout to FreeRTOS ticks, rounding a non-zero timeout
 *        up to at least one tick.
 *
 * @param timeout_ms Timeout in ms.
 * @return The timeout in ticks.
 */
static TickType_t node_ao_port_ticks_for(uint32_t timeout_ms);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_ao_err_t node_ao_port_queue_create(node_ao_t *me) {
  if (me->cfg.queue_len > (UBaseType_t)-1) {
    return NODE_AO_ERR_ARG;
  }
  QueueHandle_t queue = xQueueCreate((UBaseType_t)me->cfg.queue_len,
                                     (UBaseType_t)me->cfg.evt_size);
  if (queue == NULL) {
    return NODE_AO_ERR_NO_MEMORY;
  }
  me->queue = queue;
  return NODE_AO_OK;
}

void node_ao_port_queue_delete(node_ao_t *me) {
  if (me->queue != NULL) {
    vQueueDelete((QueueHandle_t)me->queue);
    me->queue = NULL;
  }
}

/* The one place in the firmware that creates a FreeRTOS task: every thread
 * is an active object (specs/README.md, spec 14). ESP-IDF's xTaskCreate
 * takes the stack depth in bytes. */
node_ao_err_t node_ao_port_thread_create(node_ao_t *me) {
  if (me->cfg.priority >= configMAX_PRIORITIES ||
      me->cfg.stack_bytes < configMINIMAL_STACK_SIZE ||
      me->cfg.stack_bytes > UINT32_MAX) {
    return NODE_AO_ERR_ARG;
  }
  TaskHandle_t thread = NULL;
  if (xTaskCreate(node_ao_port_thread, me->cfg.name,
                  (uint32_t)me->cfg.stack_bytes, me,
                  (UBaseType_t)me->cfg.priority, &thread) != pdPASS) {
    return NODE_AO_ERR_NO_MEMORY;
  }
  me->thread = thread;
  return NODE_AO_OK;
}

node_ao_err_t node_ao_port_send(node_ao_t *me, const void *slot) {
  return xQueueSendToBack((QueueHandle_t)me->queue, slot, 0) == pdTRUE
             ? NODE_AO_OK
             : NODE_AO_ERR_FULL;
}

node_ao_err_t node_ao_port_send_from_isr(node_ao_t *me, const void *slot) {
  BaseType_t woken = pdFALSE;
  BaseType_t sent =
      xQueueSendToBackFromISR((QueueHandle_t)me->queue, slot, &woken);
  portYIELD_FROM_ISR(woken);
  return sent == pdTRUE ? NODE_AO_OK : NODE_AO_ERR_FULL;
}

bool node_ao_port_receive(node_ao_t *me, void *slot, uint32_t timeout_ms) {
  return xQueueReceive((QueueHandle_t)me->queue, slot,
                       node_ao_port_ticks_for(timeout_ms)) == pdTRUE;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

/* Every turn waits at most NODE_AO_MAX_WAIT_MS, well inside the task
 * watchdog's timeout, so only a dispatch that hangs trips it (spec 09). */
static void node_ao_port_thread(void *arg) {
  node_ao_t *me = arg;
  const esp_err_t err = esp_task_wdt_add(NULL);
  if (err != ESP_OK) {
    ESP_LOGE(NODE_AO_PORT_LOG_TAG, "%s: not watched by the task watchdog: %s",
             me->cfg.name, esp_err_to_name(err));
  }
  for (;;) {
    (void)node_ao_run_once(me);
    if (err == ESP_OK) {
      /* Feeding a subscribed task cannot fail. */
      (void)esp_task_wdt_reset();
    }
  }
}

static TickType_t node_ao_port_ticks_for(uint32_t timeout_ms) {
  TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
  return (timeout_ms > 0 && ticks == 0) ? 1 : ticks;
}
