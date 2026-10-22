#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_private/esp_gpio_reserve.h"

#include "hal_esp_err.h"
#include "node_hal.h"

typedef struct {
  int pin;
  hal_gpio_isr_cb cb;
  void *ctx;
} isr_slot_t;

static isr_slot_t isr_slots[GPIO_NUM_MAX];
static bool isr_service_installed;

/* Flash and PSRAM claim their pins at startup, and drivers such as I2C claim
 * theirs when created, so the reservation set describes this board without a
 * hard-coded pin list. esp_gpio_is_reserved() is private ESP-IDF API; re-check
 * it when moving off the pinned IDF version. */
static bool pin_is_usable(int pin) {
  return GPIO_IS_VALID_GPIO(pin) && !esp_gpio_is_reserved(BIT64(pin));
}

static hal_err_t enable_pull(int pin, hal_pull_t pull) {
  switch (pull) {
  case HAL_PULL_UP:
    return hal_err_from_esp(gpio_pullup_en(pin));
  case HAL_PULL_DOWN:
    return hal_err_from_esp(gpio_pulldown_en(pin));
  default:
    return HAL_OK;
  }
}

hal_err_t hal_gpio_config(int pin, hal_gpio_dir_t dir, hal_pull_t pull) {
  if (!pin_is_usable(pin) || (dir != HAL_GPIO_IN && dir != HAL_GPIO_OUT) ||
      (pull != HAL_PULL_NONE && pull != HAL_PULL_UP && pull != HAL_PULL_DOWN)) {
    return HAL_ERR_ARG;
  }

  /* Outputs keep their input path so hal_gpio_read() returns the driven
   * level. gpio_config() ignores pull errors, so pulls are enabled separately
   * to catch input-only pins that have none. */
  const gpio_config_t cfg = {
      .pin_bit_mask = BIT64(pin),
      .mode = dir == HAL_GPIO_OUT ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  hal_err_t err = hal_err_from_esp(gpio_config(&cfg));
  if (err != HAL_OK) {
    return err;
  }
  return enable_pull(pin, pull);
}

hal_err_t hal_gpio_write(int pin, int level) {
  if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || !pin_is_usable(pin)) {
    return HAL_ERR_ARG;
  }
  return hal_err_from_esp(gpio_set_level(pin, level != 0));
}

int hal_gpio_read(int pin) {
  if (!GPIO_IS_VALID_GPIO(pin)) {
    return -1;
  }
  return gpio_get_level(pin);
}

static void isr_trampoline(void *arg) {
  const isr_slot_t *slot = arg;
  slot->cb(slot->pin, slot->ctx);
}

static gpio_int_type_t intr_type_for(hal_edge_t edge) {
  switch (edge) {
  case HAL_EDGE_RISING:
    return GPIO_INTR_POSEDGE;
  case HAL_EDGE_FALLING:
    return GPIO_INTR_NEGEDGE;
  default:
    return GPIO_INTR_ANYEDGE;
  }
}

static hal_err_t install_isr_service_once(void) {
  if (isr_service_installed) {
    return HAL_OK;
  }
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return hal_err_from_esp(err);
  }
  isr_service_installed = true;
  return HAL_OK;
}

hal_err_t hal_gpio_set_isr(int pin, hal_edge_t edge, hal_gpio_isr_cb cb,
                           void *ctx) {
  if (!pin_is_usable(pin) || cb == NULL ||
      (edge != HAL_EDGE_RISING && edge != HAL_EDGE_FALLING &&
       edge != HAL_EDGE_ANY)) {
    return HAL_ERR_ARG;
  }

  hal_err_t err = install_isr_service_once();
  if (err != HAL_OK) {
    return err;
  }

  /* Keep the interrupt off while the slot is rewritten, so a pending edge
   * never sees a callback paired with the wrong context. */
  gpio_intr_disable(pin);
  isr_slots[pin] = (isr_slot_t){.pin = pin, .cb = cb, .ctx = ctx};

  err = hal_err_from_esp(gpio_set_intr_type(pin, intr_type_for(edge)));
  if (err != HAL_OK) {
    return err;
  }
  return hal_err_from_esp(
      gpio_isr_handler_add(pin, isr_trampoline, &isr_slots[pin]));
}
