/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_private/esp_gpio_reserve.h"

#include "node_hal.h"
#include "node_hal_err.h"
#include "node_hal_gpio_internal.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

typedef struct {
  int pin;
  node_hal_gpio_isr_cb cb;
  void *ctx;
} isr_slot_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/**
 * @brief Callback and context registered for each pin, indexed by GPIO number.
 *
 * Written by node_hal_gpio_set_isr() with the pin's interrupt disabled, read
 * from ISR context by node_hal_gpio_isr_trampoline().
 */
static isr_slot_t g_isr_slots[GPIO_NUM_MAX] = {0};

/** @brief Whether the ESP-IDF GPIO interrupt service has been installed. */
static bool g_isr_service_installed = false;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Enable the internal pull resistor @p pull selects on @p pin.
 *
 * @param pin  GPIO number.
 * @param pull Pull resistor; NODE_HAL_GPIO_PULL_NONE enables nothing.
 * @return NODE_HAL_OK, or the error from ESP-IDF, for example for an
 *         input-only pin that has no pull resistors.
 */
static node_hal_err_t node_hal_gpio_enable_pull(int pin,
                                                node_hal_gpio_pull_t pull);

/**
 * @brief GPIO ISR handler that calls the callback registered for a pin.
 *
 * Runs in ISR context.
 *
 * @param arg The pin's entry in g_isr_slots; NULL or an empty slot is ignored.
 */
static void node_hal_gpio_isr_trampoline(void *arg);

/**
 * @brief ESP-IDF interrupt type for @p edge.
 *
 * @param edge Edge to trigger on.
 * @return The matching interrupt type; any edge for an unknown value.
 */
static gpio_int_type_t node_hal_gpio_intr_type_for(node_hal_gpio_edge_t edge);

/**
 * @brief Install the ESP-IDF GPIO interrupt service on first use.
 *
 * A service already installed by other code is accepted.
 *
 * @return NODE_HAL_OK, or the error from ESP-IDF.
 */
static node_hal_err_t node_hal_gpio_install_isr_service_once(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/* Flash and PSRAM claim their pins at startup, and drivers such as I2C claim
 * theirs when created, so the reservation set describes this board without a
 * hard-coded pin list. esp_gpio_is_reserved() is private ESP-IDF API; re-check
 * it when moving off the pinned IDF version. */
bool node_hal_gpio_pin_is_usable(int pin) {
  return GPIO_IS_VALID_GPIO(pin) && !esp_gpio_is_reserved(BIT64(pin));
}

node_hal_err_t node_hal_gpio_config(int pin, node_hal_gpio_dir_t dir,
                                    node_hal_gpio_pull_t pull) {
  if (!node_hal_gpio_pin_is_usable(pin) ||
      (dir != NODE_HAL_GPIO_IN && dir != NODE_HAL_GPIO_OUT) ||
      (pull != NODE_HAL_GPIO_PULL_NONE && pull != NODE_HAL_GPIO_PULL_UP &&
       pull != NODE_HAL_GPIO_PULL_DOWN)) {
    return NODE_HAL_ERR_ARG;
  }

  /* Outputs keep their input path so node_hal_gpio_read() returns the driven
   * level. gpio_config() ignores pull errors, so pulls are enabled separately
   * to catch input-only pins that have none. */
  const gpio_config_t cfg = {
      .pin_bit_mask = BIT64(pin),
      .mode =
          dir == NODE_HAL_GPIO_OUT ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  node_hal_err_t err = node_hal_err_from_esp(gpio_config(&cfg));
  if (err != NODE_HAL_OK) {
    return err;
  }
  return node_hal_gpio_enable_pull(pin, pull);
}

node_hal_err_t node_hal_gpio_write(int pin, int level) {
  if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || !node_hal_gpio_pin_is_usable(pin)) {
    return NODE_HAL_ERR_ARG;
  }
  return node_hal_err_from_esp(gpio_set_level(pin, level != 0));
}

int node_hal_gpio_read(int pin) {
  if (!GPIO_IS_VALID_GPIO(pin)) {
    return -1;
  }
  return gpio_get_level(pin);
}

node_hal_err_t node_hal_gpio_set_isr(int pin, node_hal_gpio_edge_t edge,
                                     node_hal_gpio_isr_cb cb, void *ctx) {
  if (!node_hal_gpio_pin_is_usable(pin) || cb == NULL ||
      (edge != NODE_HAL_GPIO_EDGE_RISING &&
       edge != NODE_HAL_GPIO_EDGE_FALLING && edge != NODE_HAL_GPIO_EDGE_ANY)) {
    return NODE_HAL_ERR_ARG;
  }

  node_hal_err_t err = node_hal_gpio_install_isr_service_once();
  if (err != NODE_HAL_OK) {
    return err;
  }

  /* Keep the interrupt off while the slot is rewritten, so a pending edge
   * never sees a callback paired with the wrong context. */
  err = node_hal_err_from_esp(gpio_intr_disable(pin));
  if (err != NODE_HAL_OK) {
    return err;
  }
  g_isr_slots[pin] = (isr_slot_t){.pin = pin, .cb = cb, .ctx = ctx};

  err = node_hal_err_from_esp(
      gpio_set_intr_type(pin, node_hal_gpio_intr_type_for(edge)));
  if (err != NODE_HAL_OK) {
    return err;
  }
  return node_hal_err_from_esp(gpio_isr_handler_add(
      pin, node_hal_gpio_isr_trampoline, &g_isr_slots[pin]));
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_hal_err_t node_hal_gpio_enable_pull(int pin,
                                                node_hal_gpio_pull_t pull) {
  switch (pull) {
    case NODE_HAL_GPIO_PULL_UP:
      return node_hal_err_from_esp(gpio_pullup_en(pin));
    case NODE_HAL_GPIO_PULL_DOWN:
      return node_hal_err_from_esp(gpio_pulldown_en(pin));
    default:
      return NODE_HAL_OK;
  }
}

static void node_hal_gpio_isr_trampoline(void *arg) {
  const isr_slot_t *slot = arg;
  if (slot == NULL || slot->cb == NULL) {
    return;
  }
  slot->cb(slot->pin, slot->ctx);
}

static gpio_int_type_t node_hal_gpio_intr_type_for(node_hal_gpio_edge_t edge) {
  switch (edge) {
    case NODE_HAL_GPIO_EDGE_RISING:
      return GPIO_INTR_POSEDGE;
    case NODE_HAL_GPIO_EDGE_FALLING:
      return GPIO_INTR_NEGEDGE;
    default:
      return GPIO_INTR_ANYEDGE;
  }
}

static node_hal_err_t node_hal_gpio_install_isr_service_once(void) {
  if (g_isr_service_installed) {
    return NODE_HAL_OK;
  }
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return node_hal_err_from_esp(err);
  }
  g_isr_service_installed = true;
  return NODE_HAL_OK;
}
