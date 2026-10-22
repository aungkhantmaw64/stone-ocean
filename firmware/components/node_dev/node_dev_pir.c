/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev_pir.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_hal.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Driver state of one PIR device; written only in the devices AO. */
typedef struct {
  int gpio;             /**< GPIO number of the sensor output. */
  bool active_high;     /**< True if a high level means motion. */
  uint32_t debounce_ms; /**< Time the level must be stable before it is
                           sampled, in ms. */
} node_dev_pir_state_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Driver state of every PIR device, by device index; written by
 *         node_dev_pir_init() in the devices AO. */
static node_dev_pir_state_t g_pir_states[NODE_CONFIG_MAX_DEVICES] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Configure a PIR device's pin and enable its any-edge interrupt.
 *
 * @param args  The device's index and config; its type must be
 *              NODE_CONFIG_DEV_PIR.
 * @param state Receives the device's node_dev_pir_state_t.
 * @param sched Zeroed; left so, as the device is sampled only after init and
 *              after interrupts.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for bad arguments, or
 *         NODE_DEV_ERR_IO if the pin or its interrupt could not be set up.
 */
static node_dev_err_t node_dev_pir_init(const node_dev_init_args_t *args,
                                        void **state, node_dev_sched_t *sched);

/**
 * @brief Read whether motion is sensed now.
 *
 * @param state    The device's node_dev_pir_state_t.
 * @param out      Receives the motion state in @c motion.
 * @param again_ms Unused; the PIR never returns NODE_DEV_PENDING.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for NULL pointers, or
 *         NODE_DEV_ERR_IO if the pin could not be read.
 */
static node_dev_err_t node_dev_pir_sample(void *state, node_dev_value_t *out,
                                          uint32_t *again_ms);

/**
 * @brief Delay before the framework samples a device after an edge.
 *
 * @param state The device's node_dev_pir_state_t.
 * @return The device's debounce_ms, or 0 for a NULL state.
 */
static uint32_t node_dev_pir_on_isr(void *state);

/**
 * @brief GPIO interrupt of a PIR pin: post the edge to the devices AO.
 *
 * ISR context. Does nothing else.
 *
 * @param pin GPIO number; unused.
 * @param ctx The device index, cast with (void *)(intptr_t).
 */
static void node_dev_pir_isr(int pin, void *ctx);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_dev_err_t node_dev_pir_register(void) {
  /* Function scope, so the table can follow the prototypes it points to;
   * static, because the framework keeps the pointer forever. */
  static const node_dev_driver_t driver = {
      .type = NODE_CONFIG_DEV_PIR,
      .sample_sig = NODE_DEV_SIG_MOTION,
      .init = node_dev_pir_init,
      .sample = node_dev_pir_sample,
      .set_onoff = NULL,
      .on_isr = node_dev_pir_on_isr,
  };
  return node_dev_register_driver(&driver);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_dev_err_t node_dev_pir_init(const node_dev_init_args_t *args,
                                        void **state, node_dev_sched_t *sched) {
  if ((args == NULL) || (args->cfg == NULL) || (state == NULL) ||
      (sched == NULL) || (args->dev_index < 0) ||
      (args->dev_index >= NODE_CONFIG_MAX_DEVICES) ||
      (args->cfg->type != NODE_CONFIG_DEV_PIR)) {
    return NODE_DEV_ERR_ARG;
  }
  const node_config_pir_t *cfg = &args->cfg->pir;
  node_dev_pir_state_t *pir = &g_pir_states[args->dev_index];
  pir->gpio = cfg->gpio;
  pir->active_high = cfg->active_high;
  pir->debounce_ms = cfg->debounce_ms;

  if (node_hal_gpio_config(cfg->gpio, NODE_HAL_GPIO_IN, cfg->pull) !=
      NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  if (node_hal_gpio_set_isr(cfg->gpio, NODE_HAL_GPIO_EDGE_ANY, node_dev_pir_isr,
                            (void *)(intptr_t)args->dev_index) != NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  *state = pir;
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_pir_sample(void *state, node_dev_value_t *out,
                                          uint32_t *again_ms) {
  (void)again_ms;
  if ((state == NULL) || (out == NULL)) {
    return NODE_DEV_ERR_ARG;
  }
  const node_dev_pir_state_t *pir = state;
  const int level = node_hal_gpio_read(pir->gpio);
  if (level < 0) {
    return NODE_DEV_ERR_IO;
  }
  const bool high = (level != 0);
  out->motion = (high == pir->active_high);
  return NODE_DEV_OK;
}

static uint32_t node_dev_pir_on_isr(void *state) {
  if (state == NULL) {
    return 0;
  }
  const node_dev_pir_state_t *pir = state;
  return pir->debounce_ms;
}

static void node_dev_pir_isr(int pin, void *ctx) {
  (void)pin;
  /* A full queue drops the edge; nothing can be logged from an ISR, and the
   * next edge samples the pin again. */
  (void)node_dev_post_from_isr((int)(intptr_t)ctx);
}
