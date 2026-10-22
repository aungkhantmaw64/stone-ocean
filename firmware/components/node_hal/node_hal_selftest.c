/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "node_hal.h"
#include "node_hal_selftest.h"

#if CONFIG_NODE_HAL_SELFTEST

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief I2C controller used for the bus scan. */
#define I2C_PORT 0

/** @brief Bus clock for the scan, in Hz. */
#define I2C_FREQ_HZ 100000

/** @brief Time to wait for each address to acknowledge, in ms. */
#define I2C_PROBE_TIMEOUT_MS 50

/** @brief First address scanned; 0x00–0x07 are reserved by the I2C
 *         specification. */
#define I2C_SCAN_FIRST_ADDR 0x08

/** @brief Last address scanned; 0x78–0x7F are reserved by the I2C
 *         specification. */
#define I2C_SCAN_LAST_ADDR 0x77

/** @brief 7-bit address of the SHTC3 temperature and humidity sensor. */
#define SHTC3_ADDR7 0x70

/** @brief Value of CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO meaning "do not
 *         toggle an output". */
#define NO_OUTPUT_GPIO (-1)

/** @brief Time between output toggles, and the longest wait for a button
 *         edge, in ms. */
#define TOGGLE_PERIOD_MS 500

/** @brief Button edges buffered between the ISR and the self-test task. */
#define EDGE_QUEUE_LENGTH 8

/** @brief Stack size of the self-test task, in bytes. */
#define TASK_STACK_BYTES 3072

/** @brief FreeRTOS priority of the self-test task. */
#define TASK_PRIORITY 5

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Log tag for self-test output and name of the self-test task. */
static const char *const TAG = "node_hal_selftest";

/**
 * @brief Pin numbers of button edges.
 *
 * Sent from ISR context by node_hal_selftest_on_button_edge() and received by
 * the self-test task.
 */
static QueueHandle_t g_edge_queue = NULL;

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_hal_selftest_scan_i2c(void) {
  node_hal_i2c_bus_t bus = NULL;
  node_hal_err_t err = node_hal_i2c_bus_init(
      I2C_PORT, CONFIG_NODE_HAL_SELFTEST_I2C_SDA,
      CONFIG_NODE_HAL_SELFTEST_I2C_SCL, I2C_FREQ_HZ, &bus);
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "I2C bus init on SDA %d / SCL %d failed: %d",
             CONFIG_NODE_HAL_SELFTEST_I2C_SDA, CONFIG_NODE_HAL_SELFTEST_I2C_SCL,
             err);
    return;
  }

  ESP_LOGI(TAG, "I2C scan 0x%02X-0x%02X on SDA %d / SCL %d",
           I2C_SCAN_FIRST_ADDR, I2C_SCAN_LAST_ADDR,
           CONFIG_NODE_HAL_SELFTEST_I2C_SDA, CONFIG_NODE_HAL_SELFTEST_I2C_SCL);
  int found = 0;
  bool shtc3_found = false;
  for (uint8_t addr = I2C_SCAN_FIRST_ADDR; addr <= I2C_SCAN_LAST_ADDR; addr++) {
    err = node_hal_i2c_probe(bus, addr, I2C_PROBE_TIMEOUT_MS);
    if (err == NODE_HAL_OK) {
      ESP_LOGI(TAG, "  0x%02X acknowledged", addr);
      found++;
      shtc3_found |= addr == SHTC3_ADDR7;
    } else if (err != NODE_HAL_ERR_NACK) {
      ESP_LOGW(TAG, "  0x%02X probe failed: %d", addr, err);
    }
  }
  ESP_LOGI(TAG, "I2C scan done: %d device(s); SHTC3 at 0x%02X %s", found,
           SHTC3_ADDR7, shtc3_found ? "found" : "NOT found");
}

static void node_hal_selftest_on_button_edge(int pin, void *ctx) {
  (void)ctx;
  BaseType_t woken = pdFALSE;
  /* An ISR must not block, so an edge that arrives while the queue is full
   * is dropped. */
  (void)xQueueSendFromISR(g_edge_queue, &pin, &woken);
  portYIELD_FROM_ISR(woken);
}

static bool node_hal_selftest_watch_button(void) {
  const int pin = CONFIG_NODE_HAL_SELFTEST_BUTTON_GPIO;
  node_hal_err_t err =
      node_hal_gpio_config(pin, NODE_HAL_GPIO_IN, NODE_HAL_GPIO_PULL_UP);
  if (err == NODE_HAL_OK) {
    err = node_hal_gpio_set_isr(pin, NODE_HAL_GPIO_EDGE_ANY,
                                node_hal_selftest_on_button_edge, NULL);
  }
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "button GPIO %d setup failed: %d", pin, err);
    return false;
  }
  ESP_LOGI(TAG, "logging edges on button GPIO %d (level now %d)", pin,
           node_hal_gpio_read(pin));
  return true;
}

static bool node_hal_selftest_prepare_output(void) {
  const int pin = CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO;
  if (pin == NO_OUTPUT_GPIO) {
    ESP_LOGI(TAG, "no output GPIO configured; not toggling");
    return false;
  }
  node_hal_err_t err =
      node_hal_gpio_config(pin, NODE_HAL_GPIO_OUT, NODE_HAL_GPIO_PULL_NONE);
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "output GPIO %d setup failed: %d", pin, err);
    return false;
  }
  ESP_LOGI(TAG, "toggling output GPIO %d every %d ms", pin, TOGGLE_PERIOD_MS);
  return true;
}

static void node_hal_selftest_task(void *arg) {
  (void)arg;
  node_hal_selftest_scan_i2c();
  /* A failed button setup is already logged; the output toggle still runs. */
  (void)node_hal_selftest_watch_button();
  bool toggling = node_hal_selftest_prepare_output();

  int level = 0;
  uint32_t next_toggle_ms = node_hal_time_ms() + TOGGLE_PERIOD_MS;
  for (;;) {
    int pin = 0;
    if (xQueueReceive(g_edge_queue, &pin, pdMS_TO_TICKS(TOGGLE_PERIOD_MS)) ==
        pdTRUE) {
      ESP_LOGI(TAG, "edge on GPIO %d at %lu ms, level now %d", pin,
               (unsigned long)node_hal_time_ms(), node_hal_gpio_read(pin));
    }
    if (toggling && (int32_t)(node_hal_time_ms() - next_toggle_ms) >= 0) {
      level = !level;
      node_hal_err_t err =
          node_hal_gpio_write(CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO, level);
      if (err != NODE_HAL_OK) {
        ESP_LOGE(TAG, "output GPIO %d write failed: %d; stopped toggling",
                 CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO, err);
        toggling = false;
      }
      next_toggle_ms += TOGGLE_PERIOD_MS;
    }
  }
}

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_hal_selftest_start(void) {
  if (g_edge_queue != NULL) {
    ESP_LOGW(TAG, "self-test already started");
    return;
  }
  g_edge_queue = xQueueCreate(EDGE_QUEUE_LENGTH, sizeof(int));
  if (g_edge_queue == NULL) {
    ESP_LOGE(TAG, "could not create the button edge queue");
    return;
  }
  if (xTaskCreate(node_hal_selftest_task, TAG, TASK_STACK_BYTES, NULL,
                  TASK_PRIORITY, NULL) != pdPASS) {
    ESP_LOGE(TAG, "could not start the self-test task");
    vQueueDelete(g_edge_queue);
    g_edge_queue = NULL;
  }
}

#else

void node_hal_selftest_start(void) {}

#endif
