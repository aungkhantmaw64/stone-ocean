/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_hal.h"
#include "node_hal_board.h"
#include "node_health.h"
#include "node_health_internal.h"
#include "node_health_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Name of the health AO's thread, also used in logs. */
#define NODE_HEALTH_AO_NAME "node_health"

/** @brief FreeRTOS priority of the health AO; below the status AO (4), as a
 *         5 s hold can wait a few ms. */
#define NODE_HEALTH_AO_PRIORITY 3

/** @brief Stack size of the health AO's thread, in bytes. A factory reset
 *         runs in this thread: the hooks, the LittleFS erase and the NVS
 *         erase. ESP-IDF's main task (3.5 KB) already mounts and reads
 *         LittleFS at boot, so 4 KB leaves room for the hooks. */
#define NODE_HEALTH_AO_STACK_BYTES 4096

/** @brief Events the health AO's queue holds; enough for a bouncing press. */
#define NODE_HEALTH_AO_QUEUE_LEN 8

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Private signals of the health AO. */
enum {
  NODE_HEALTH_AO_SIG_EDGE = NODE_AO_SIG_USER, /**< The reset button's pin
                                                 changed level; from the
                                                 ISR. */
  NODE_HEALTH_AO_SIG_HOLD_DONE,               /**< The button has been held
                                                 for
                                                 NODE_HEALTH_RESET_HOLD_MS;
                                                 from the hold timer. */
};

/** @brief The health active object. */
typedef struct {
  node_ao_t super;               /**< Framework part; must be first. */
  bool pin_ready;                /**< True once the button's pin and
                                    interrupt are set up; while false,
                                    edges are ignored. */
  bool wait_for_release;         /**< True while a press that was already
                                    held at boot is still held; it does not
                                    count. */
  node_ao_time_evt_t hold_timer; /**< One-shot, armed for
                                    NODE_HEALTH_RESET_HOLD_MS while the
                                    button is held. */
} node_health_ao_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief True once node_health_ao_start() started the health AO; written
 *         once, from task context. */
static bool g_started = false;

/** @brief The health AO; its state is touched only in its own thread. */
static node_health_ao_t g_health = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle one event of the health AO.
 *
 * @param me The health AO.
 * @param e  The event.
 */
static void node_health_ao_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Set the button's pin up as an input with an interrupt on both
 *        edges; on failure, log it and leave the AO ignoring edges.
 *
 * @param me The health AO.
 */
static void node_health_ao_init(node_health_ao_t *me);

/**
 * @brief Start the hold timer on a press and cancel it on a release.
 *
 * @param me The health AO.
 */
static void node_health_ao_on_edge(node_health_ao_t *me);

/**
 * @brief Factory-reset the node if the button is still held when the hold
 *        timer fires.
 *
 * @param me The health AO.
 */
static void node_health_ao_on_hold_done(node_health_ao_t *me);

/**
 * @brief Whether the reset button is pressed now.
 *
 * @return True if pressed; false if released or unreadable.
 */
static bool node_health_ao_button_pressed(void);

/**
 * @brief Post an edge event to the health AO. Runs in ISR context.
 *
 * @param pin GPIO number of the button; unused.
 * @param ctx Unused.
 */
static void node_health_ao_isr(int pin, void *ctx);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_health_err_t node_health_ao_start(void) {
  if (g_started) {
    return NODE_HEALTH_ERR_STATE;
  }
  if (node_ao_time_evt_init(&g_health.hold_timer, &g_health.super,
                            NODE_HEALTH_AO_SIG_HOLD_DONE) != NODE_AO_OK) {
    return NODE_HEALTH_ERR_STATE;
  }
  const node_ao_config_t ao_cfg = {
      .name = NODE_HEALTH_AO_NAME,
      .priority = NODE_HEALTH_AO_PRIORITY,
      .stack_bytes = NODE_HEALTH_AO_STACK_BYTES,
      .queue_len = NODE_HEALTH_AO_QUEUE_LEN,
      .evt_size = sizeof(node_ao_evt_t),
  };
  if (node_ao_start(&g_health.super, node_health_ao_dispatch, &ao_cfg) !=
      NODE_AO_OK) {
    return NODE_HEALTH_ERR_STATE;
  }
  g_started = true;
  return NODE_HEALTH_OK;
}

void node_health_ao_reset(void) {
  g_started = false;
  (void)memset(&g_health, 0, sizeof(g_health));
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_health_ao_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  if ((me == NULL) || (e == NULL)) {
    return;
  }
  node_health_ao_t *health = (node_health_ao_t *)me;
  if (e->sig == NODE_AO_SIG_INIT) {
    node_health_ao_init(health);
    return;
  }
  if (!health->pin_ready) {
    return;
  }
  switch (e->sig) {
    case NODE_HEALTH_AO_SIG_EDGE:
      node_health_ao_on_edge(health);
      break;
    case NODE_HEALTH_AO_SIG_HOLD_DONE:
      node_health_ao_on_hold_done(health);
      break;
    default:
      break;
  }
}

static void node_health_ao_init(node_health_ao_t *me) {
  node_hal_err_t err =
      node_hal_gpio_config(NODE_HAL_BOARD_RESET_BUTTON_GPIO, NODE_HAL_GPIO_IN,
                           NODE_HAL_GPIO_PULL_NONE);
  if (err == NODE_HAL_OK) {
    err =
        node_hal_gpio_set_isr(NODE_HAL_BOARD_RESET_BUTTON_GPIO,
                              NODE_HAL_GPIO_EDGE_ANY, node_health_ao_isr, NULL);
  }
  if (err != NODE_HAL_OK) {
    node_health_log_button_setup_failed(NODE_HAL_BOARD_RESET_BUTTON_GPIO, err);
    return;
  }
  me->pin_ready = true;
  me->wait_for_release = node_health_ao_button_pressed();
  node_health_log_button_started(NODE_HAL_BOARD_RESET_BUTTON_GPIO);
}

static void node_health_ao_on_edge(node_health_ao_t *me) {
  if (!node_health_ao_button_pressed()) {
    me->wait_for_release = false;
    if (node_ao_time_evt_is_armed(&me->hold_timer)) {
      /* Disarming an initialised timer cannot fail. */
      (void)node_ao_time_evt_disarm(&me->hold_timer);
      node_health_log_button_released_early();
    }
    return;
  }
  if (me->wait_for_release || node_ao_time_evt_is_armed(&me->hold_timer)) {
    return;
  }
  /* NODE_HEALTH_RESET_HOLD_MS is far below the arm limit of INT32_MAX, and
   * the timer is initialised at start, so arming cannot fail. */
  (void)node_ao_time_evt_arm(&me->hold_timer, NODE_HEALTH_RESET_HOLD_MS, 0);
  node_health_log_button_held(NODE_HEALTH_RESET_HOLD_MS);
}

static void node_health_ao_on_hold_done(node_health_ao_t *me) {
  (void)me;
  /* A release edge can be lost to a full queue, so the level decides. */
  if (!node_health_ao_button_pressed()) {
    return;
  }
  node_health_factory_reset();
}

static bool node_health_ao_button_pressed(void) {
  const int level = node_hal_gpio_read(NODE_HAL_BOARD_RESET_BUTTON_GPIO);
  if (level < 0) {
    return false;
  }
  return (level != 0) == NODE_HAL_BOARD_RESET_BUTTON_ACTIVE_HIGH;
}

static void node_health_ao_isr(int pin, void *ctx) {
  (void)pin;
  (void)ctx;
  const node_ao_evt_t evt = {.sig = NODE_HEALTH_AO_SIG_EDGE};
  /* A full queue drops the edge; the level is read again on the next edge
   * and when the hold timer fires. */
  (void)node_ao_post_from_isr(&g_health.super, &evt, sizeof(evt));
}
