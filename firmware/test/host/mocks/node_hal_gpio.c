/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>

#include "node_hal_mock.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

typedef struct {
  bool configured;
  node_hal_gpio_dir_t dir;
  int level;
  node_hal_gpio_isr_cb cb;
  void *ctx;
  node_hal_gpio_edge_t edge;
  size_t write_count; /**< Successful writes since the last reset. */
} node_hal_gpio_mock_pin_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Simulated state of every pin, indexed by GPIO number. */
static node_hal_gpio_mock_pin_t g_pins[NODE_HAL_MOCK_GPIO_COUNT] = {0};

/** @brief Error the next write to a valid output pin returns; NODE_HAL_OK
 *         if none is pending. Set by node_hal_gpio_mock_fail_next_write(). */
static node_hal_err_t g_next_write_error = NODE_HAL_OK;

/** @brief Error the next config of a valid pin returns; NODE_HAL_OK if none
 *         is pending. Set by node_hal_gpio_mock_fail_next_config(). */
static node_hal_err_t g_next_config_error = NODE_HAL_OK;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Whether @p pin is a GPIO number the mock simulates.
 *
 * @param pin GPIO number.
 * @return True if 0 <= @p pin < NODE_HAL_MOCK_GPIO_COUNT.
 */
static bool node_hal_gpio_pin_in_range(int pin);

/**
 * @brief Whether a level change from @p from to @p to triggers @p edge.
 *
 * @param edge Edge the pin's interrupt watches for.
 * @param from Level before the change, 0 or 1.
 * @param to   Level after the change, 0 or 1.
 * @return True if the change is an edge of that kind.
 */
static bool node_hal_gpio_edge_matches(node_hal_gpio_edge_t edge, int from,
                                       int to);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_hal_gpio_mock_reset(void) {
  for (int pin = 0; pin < NODE_HAL_MOCK_GPIO_COUNT; pin++) {
    g_pins[pin] = (node_hal_gpio_mock_pin_t){0};
  }
  g_next_write_error = NODE_HAL_OK;
  g_next_config_error = NODE_HAL_OK;
}

node_hal_err_t node_hal_gpio_config(int pin, node_hal_gpio_dir_t dir,
                                    node_hal_gpio_pull_t pull) {
  if (!node_hal_gpio_pin_in_range(pin) ||
      (dir != NODE_HAL_GPIO_IN && dir != NODE_HAL_GPIO_OUT) ||
      (pull != NODE_HAL_GPIO_PULL_NONE && pull != NODE_HAL_GPIO_PULL_UP &&
       pull != NODE_HAL_GPIO_PULL_DOWN)) {
    return NODE_HAL_ERR_ARG;
  }
  if (g_next_config_error != NODE_HAL_OK) {
    const node_hal_err_t err = g_next_config_error;
    g_next_config_error = NODE_HAL_OK;
    return err;
  }
  g_pins[pin].configured = true;
  g_pins[pin].dir = dir;
  g_pins[pin].level = pull == NODE_HAL_GPIO_PULL_UP ? 1 : 0;
  return NODE_HAL_OK;
}

/* Stricter than the ESP implementation: writing a pin that is not configured
 * as output is rejected, so tests catch drivers that skip configuration. */
node_hal_err_t node_hal_gpio_write(int pin, int level) {
  if (!node_hal_gpio_pin_in_range(pin) || !g_pins[pin].configured ||
      g_pins[pin].dir != NODE_HAL_GPIO_OUT) {
    return NODE_HAL_ERR_ARG;
  }
  if (g_next_write_error != NODE_HAL_OK) {
    const node_hal_err_t err = g_next_write_error;
    g_next_write_error = NODE_HAL_OK;
    return err;
  }
  g_pins[pin].level = level != 0;
  g_pins[pin].write_count++;
  return NODE_HAL_OK;
}

int node_hal_gpio_read(int pin) {
  if (!node_hal_gpio_pin_in_range(pin)) {
    return -1;
  }
  return g_pins[pin].level;
}

node_hal_err_t node_hal_gpio_set_isr(int pin, node_hal_gpio_edge_t edge,
                                     node_hal_gpio_isr_cb cb, void *ctx) {
  if (!node_hal_gpio_pin_in_range(pin) || cb == NULL ||
      (edge != NODE_HAL_GPIO_EDGE_RISING &&
       edge != NODE_HAL_GPIO_EDGE_FALLING && edge != NODE_HAL_GPIO_EDGE_ANY)) {
    return NODE_HAL_ERR_ARG;
  }
  g_pins[pin].edge = edge;
  g_pins[pin].cb = cb;
  g_pins[pin].ctx = ctx;
  return NODE_HAL_OK;
}

void node_hal_gpio_mock_set_input(int pin, int level) {
  if (!node_hal_gpio_pin_in_range(pin)) {
    return;
  }
  node_hal_gpio_mock_pin_t *p = &g_pins[pin];
  int from = p->level;
  int to = level != 0;
  p->level = to;
  if (p->cb != NULL && node_hal_gpio_edge_matches(p->edge, from, to)) {
    p->cb(pin, p->ctx);
  }
}

size_t node_hal_gpio_mock_write_count(int pin) {
  if (!node_hal_gpio_pin_in_range(pin)) {
    return 0;
  }
  return g_pins[pin].write_count;
}

void node_hal_gpio_mock_fail_next_write(node_hal_err_t err) {
  g_next_write_error = err;
}

void node_hal_gpio_mock_fail_next_config(node_hal_err_t err) {
  g_next_config_error = err;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_hal_gpio_pin_in_range(int pin) {
  return pin >= 0 && pin < NODE_HAL_MOCK_GPIO_COUNT;
}

static bool node_hal_gpio_edge_matches(node_hal_gpio_edge_t edge, int from,
                                       int to) {
  switch (edge) {
    case NODE_HAL_GPIO_EDGE_RISING:
      return from == 0 && to == 1;
    case NODE_HAL_GPIO_EDGE_FALLING:
      return from == 1 && to == 0;
    default:
      return from != to;
  }
}
