/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev_led.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_hal.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Driver state of one LED device; written only in the devices AO. */
typedef struct {
  int gpio;         /**< GPIO number that drives the LED. */
  bool active_high; /**< True if a high level turns the LED on. */
  bool on;          /**< True while the LED is switched on; follows the last
                       successful pin write. */
} node_dev_led_state_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Driver state of every LED device, by device index; written by the
 *         LED driver's methods in the devices AO. */
static node_dev_led_state_t g_led_states[NODE_CONFIG_MAX_DEVICES] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Configure an LED device's pin as output and drive it to default_on.
 *
 * @param args  The device's index and config; its type must be
 *              NODE_CONFIG_DEV_LED.
 * @param state Receives the device's node_dev_led_state_t.
 * @param sched Zeroed; left so, as the LED is sampled only once after init.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for bad arguments, or
 *         NODE_DEV_ERR_IO if the pin could not be set up or driven.
 */
static node_dev_err_t node_dev_led_init(const node_dev_init_args_t *args,
                                        void **state, node_dev_sched_t *sched);

/**
 * @brief Report whether the LED is switched on.
 *
 * @param state    The device's node_dev_led_state_t.
 * @param out      Receives the state in @c on.
 * @param again_ms Unused; the LED never returns NODE_DEV_PENDING.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_ARG for NULL pointers.
 */
static node_dev_err_t node_dev_led_sample(void *state, node_dev_value_t *out,
                                          uint32_t *again_ms);

/**
 * @brief Switch the LED on or off; does nothing if it is already so.
 *
 * @param state The device's node_dev_led_state_t.
 * @param on    True to switch on.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for a NULL state, or
 *         NODE_DEV_ERR_IO if the pin could not be driven; the state is then
 *         unchanged.
 */
static node_dev_err_t node_dev_led_set_onoff(void *state, bool on);

/**
 * @brief Drive an LED's pin to the level that shows @p on.
 *
 * @param led The device's state; its @c gpio and @c active_high are used.
 * @param on  True for the level that lights the LED.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if the pin could not be driven.
 */
static node_dev_err_t node_dev_led_write(const node_dev_led_state_t *led,
                                         bool on);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_dev_err_t node_dev_led_register(void) {
  /* Function scope, so the table can follow the prototypes it points to;
   * static, because the framework keeps the pointer forever. */
  static const node_dev_driver_t driver = {
      .type = NODE_CONFIG_DEV_LED,
      .sample_sig = NODE_DEV_SIG_ONOFF_CHANGED,
      .init = node_dev_led_init,
      .sample = node_dev_led_sample,
      .set_onoff = node_dev_led_set_onoff,
      .on_isr = NULL,
  };
  return node_dev_register_driver(&driver);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_dev_err_t node_dev_led_init(const node_dev_init_args_t *args,
                                        void **state, node_dev_sched_t *sched) {
  if ((args == NULL) || (args->cfg == NULL) || (state == NULL) ||
      (sched == NULL) || (args->dev_index < 0) ||
      (args->dev_index >= NODE_CONFIG_MAX_DEVICES) ||
      (args->cfg->type != NODE_CONFIG_DEV_LED)) {
    return NODE_DEV_ERR_ARG;
  }
  const node_config_led_t *cfg = &args->cfg->led;
  node_dev_led_state_t *led = &g_led_states[args->dev_index];
  led->gpio = cfg->gpio;
  led->active_high = cfg->active_high;

  if (node_hal_gpio_config(cfg->gpio, NODE_HAL_GPIO_OUT,
                           NODE_HAL_GPIO_PULL_NONE) != NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  const node_dev_err_t err = node_dev_led_write(led, cfg->default_on);
  if (err != NODE_DEV_OK) {
    return err;
  }
  led->on = cfg->default_on;
  *state = led;
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_led_sample(void *state, node_dev_value_t *out,
                                          uint32_t *again_ms) {
  (void)again_ms;
  if ((state == NULL) || (out == NULL)) {
    return NODE_DEV_ERR_ARG;
  }
  const node_dev_led_state_t *led = state;
  out->on = led->on;
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_led_set_onoff(void *state, bool on) {
  if (state == NULL) {
    return NODE_DEV_ERR_ARG;
  }
  node_dev_led_state_t *led = state;
  if (led->on == on) {
    return NODE_DEV_OK;
  }
  const node_dev_err_t err = node_dev_led_write(led, on);
  if (err != NODE_DEV_OK) {
    return err;
  }
  led->on = on;
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_led_write(const node_dev_led_state_t *led,
                                         bool on) {
  const int level = (on == led->active_high) ? 1 : 0;
  if (node_hal_gpio_write(led->gpio, level) != NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  return NODE_DEV_OK;
}
