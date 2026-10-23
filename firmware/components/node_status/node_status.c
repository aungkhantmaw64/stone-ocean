/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_hal.h"
#include "node_hal_board.h"
#include "node_status_internal.h"
#include "node_status_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Name of the status AO's thread, also used in logs. */
#define NODE_STATUS_AO_NAME "node_status"

/** @brief FreeRTOS priority of the status AO; below the devices AO (5), as
 *         an indicator can wait. */
#define NODE_STATUS_AO_PRIORITY 4

/** @brief Stack size of the status AO's thread, in bytes (spec 16). Measured
 *         peak on the ESP32 is about 2.1 KB, nearly all of it ESP_LOG's
 *         formatting. */
#define NODE_STATUS_AO_STACK_BYTES 3072

/** @brief Events the status AO's queue holds (spec 16). */
#define NODE_STATUS_AO_QUEUE_LEN 4

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Private signals of the status AO. */
enum {
  NODE_STATUS_SIG_SET = NODE_AO_SIG_USER, /**< Command from
                                             node_status_led_set(). */
  NODE_STATUS_SIG_BLINK,                  /**< Command from
                                             node_status_led_blink(). */
  NODE_STATUS_SIG_PHASE_END,              /**< The current blink phase is
                                             over; from the phase timer. */
};

/** @brief What the status LED is doing. */
typedef enum {
  NODE_STATUS_MODE_OFF,   /**< Steady off. */
  NODE_STATUS_MODE_ON,    /**< Steady on. */
  NODE_STATUS_MODE_BLINK, /**< Blinking with the AO's on and off times. */
} node_status_mode_t;

/** @brief Command to switch the LED steadily, posted by
 *         node_status_led_set(). */
typedef struct {
  node_ao_evt_t super; /**< Signal NODE_STATUS_SIG_SET. */
  bool on;             /**< True for steady on. */
} node_status_set_evt_t;

/** @brief Command to blink the LED, posted by node_status_led_blink(). */
typedef struct {
  node_ao_evt_t super; /**< Signal NODE_STATUS_SIG_BLINK. */
  uint16_t on_ms;      /**< On time of each period, in ms; at least
                          NODE_STATUS_BLINK_MIN_MS. */
  uint16_t off_ms;     /**< Off time of each period, in ms; at least
                          NODE_STATUS_BLINK_MIN_MS. */
} node_status_blink_evt_t;

_Static_assert(sizeof(node_status_blink_evt_t) <= NODE_AO_MAX_EVT_SIZE,
               "node_status_blink_evt_t must fit in an AO queue slot");
_Static_assert(sizeof(node_status_set_evt_t) <= sizeof(node_status_blink_evt_t),
               "the status AO's evt_size must hold its largest event");

/** @brief The status active object. */
typedef struct {
  node_ao_t super;                /**< Framework part; must be first. */
  bool pin_ready;                 /**< True once the LED's pin is set up; while
                                     false, commands are ignored. */
  node_ao_time_evt_t phase_timer; /**< One-shot, armed to the end of the
                                     current blink phase; disarmed unless
                                     blinking. */
  node_status_mode_t mode;        /**< What the LED was last told to do. */
  uint16_t on_ms;                 /**< On time while blinking, in ms. */
  uint16_t off_ms;                /**< Off time while blinking, in ms. */
  bool lit;                       /**< True during a blink's on phase. */
  bool write_failing;             /**< True from a failed pin write until
                                     the next successful one. */
} node_status_ao_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief True once node_status_start() started the status AO; written once,
 *         from task context. */
static bool g_started = false;

/** @brief The status AO; its state is touched only in its own thread. */
static node_status_ao_t g_status = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle one event of the status AO.
 *
 * @param me The status AO.
 * @param e  The event.
 */
static void node_status_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Set the LED's pin up as an output and switch the LED off; on
 *        failure, log it and leave the AO ignoring commands.
 *
 * @param me The status AO.
 */
static void node_status_init(node_status_ao_t *me);

/**
 * @brief Switch the LED steadily on or off. Logs the change, and does
 *        nothing if the LED is already so and its last write worked.
 *
 * @param me The status AO.
 * @param on True for on.
 */
static void node_status_set_steady(node_status_ao_t *me, bool on);

/**
 * @brief Start blinking with new times, from the on phase. Logs the change
 *        unless the LED already blinks with the same times.
 *
 * @param me     The status AO.
 * @param on_ms  On time of each period, in ms.
 * @param off_ms Off time of each period, in ms.
 */
static void node_status_start_blink(node_status_ao_t *me, uint16_t on_ms,
                                    uint16_t off_ms);

/**
 * @brief End the current blink phase: toggle the LED and time the next one.
 *
 * @param me The status AO.
 */
static void node_status_end_phase(node_status_ao_t *me);

/**
 * @brief Light or darken the LED for a blink phase, log it at DEBUG, and arm
 *        the phase timer to its end.
 *
 * @param me  The status AO.
 * @param lit True for the on phase.
 */
static void node_status_begin_phase(node_status_ao_t *me, bool lit);

/**
 * @brief Drive the LED's pin to the level that shows @p lit, and log when
 *        writes start failing or work again.
 *
 * @param me  The status AO.
 * @param lit True for the level that lights the LED.
 */
static void node_status_drive(node_status_ao_t *me, bool lit);

/**
 * @brief Post a command to the status AO.
 *
 * @param evt  The command; starts with a node_ao_evt_t.
 * @param size Size of @p evt, in bytes.
 * @return NODE_STATUS_OK, NODE_STATUS_ERR_STATE before start, or
 *         NODE_STATUS_ERR_FULL if the queue is full.
 */
static node_status_err_t node_status_post(const void *evt, size_t size);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_status_err_t node_status_start(void) {
  if (g_started) {
    return NODE_STATUS_ERR_STATE;
  }
  if (node_ao_time_evt_init(&g_status.phase_timer, &g_status.super,
                            NODE_STATUS_SIG_PHASE_END) != NODE_AO_OK) {
    return NODE_STATUS_ERR_STATE;
  }
  const node_ao_config_t ao_cfg = {
      .name = NODE_STATUS_AO_NAME,
      .priority = NODE_STATUS_AO_PRIORITY,
      .stack_bytes = NODE_STATUS_AO_STACK_BYTES,
      .queue_len = NODE_STATUS_AO_QUEUE_LEN,
      .evt_size = sizeof(node_status_blink_evt_t),
  };
  if (node_ao_start(&g_status.super, node_status_dispatch, &ao_cfg) !=
      NODE_AO_OK) {
    return NODE_STATUS_ERR_STATE;
  }
  g_started = true;
  return NODE_STATUS_OK;
}

node_status_err_t node_status_led_set(bool on) {
  const node_status_set_evt_t evt = {
      .super = {.sig = NODE_STATUS_SIG_SET},
      .on = on,
  };
  return node_status_post(&evt, sizeof(evt));
}

node_status_err_t node_status_led_blink(uint16_t on_ms, uint16_t off_ms) {
  if ((on_ms < NODE_STATUS_BLINK_MIN_MS) ||
      (off_ms < NODE_STATUS_BLINK_MIN_MS)) {
    return NODE_STATUS_ERR_ARG;
  }
  const node_status_blink_evt_t evt = {
      .super = {.sig = NODE_STATUS_SIG_BLINK},
      .on_ms = on_ms,
      .off_ms = off_ms,
  };
  return node_status_post(&evt, sizeof(evt));
}

void node_status_reset(void) {
  g_started = false;
  (void)memset(&g_status, 0, sizeof(g_status));
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_status_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  if ((me == NULL) || (e == NULL)) {
    return;
  }
  node_status_ao_t *status = (node_status_ao_t *)me;
  if (e->sig == NODE_AO_SIG_INIT) {
    node_status_init(status);
    return;
  }
  if (!status->pin_ready) {
    return;
  }
  switch (e->sig) {
    case NODE_STATUS_SIG_SET:
      node_status_set_steady(status, ((const node_status_set_evt_t *)e)->on);
      break;
    case NODE_STATUS_SIG_BLINK: {
      const node_status_blink_evt_t *blink = (const node_status_blink_evt_t *)e;
      node_status_start_blink(status, blink->on_ms, blink->off_ms);
      break;
    }
    case NODE_STATUS_SIG_PHASE_END:
      node_status_end_phase(status);
      break;
    default:
      break;
  }
}

static void node_status_init(node_status_ao_t *me) {
  const node_hal_err_t err =
      node_hal_gpio_config(NODE_HAL_BOARD_STATUS_LED_GPIO, NODE_HAL_GPIO_OUT,
                           NODE_HAL_GPIO_PULL_NONE);
  if (err != NODE_HAL_OK) {
    node_status_log_setup_failed(NODE_HAL_BOARD_STATUS_LED_GPIO, err);
    return;
  }
  me->pin_ready = true;
  me->mode = NODE_STATUS_MODE_OFF;
  node_status_drive(me, false);
  node_status_log_started(NODE_HAL_BOARD_STATUS_LED_GPIO);
}

static void node_status_set_steady(node_status_ao_t *me, bool on) {
  const node_status_mode_t mode =
      on ? NODE_STATUS_MODE_ON : NODE_STATUS_MODE_OFF;
  if (mode == me->mode) {
    /* Retry the level after a failed write, as a steady LED has no next
     * phase that would write it again. */
    if (me->write_failing) {
      node_status_drive(me, on);
    }
    return;
  }
  /* Disarming an initialised timer cannot fail. */
  (void)node_ao_time_evt_disarm(&me->phase_timer);
  me->mode = mode;
  node_status_log_steady(on);
  node_status_drive(me, on);
}

static void node_status_start_blink(node_status_ao_t *me, uint16_t on_ms,
                                    uint16_t off_ms) {
  const bool same = (me->mode == NODE_STATUS_MODE_BLINK) &&
                    (me->on_ms == on_ms) && (me->off_ms == off_ms);
  me->mode = NODE_STATUS_MODE_BLINK;
  me->on_ms = on_ms;
  me->off_ms = off_ms;
  if (!same) {
    node_status_log_blink(on_ms, off_ms);
  }
  node_status_begin_phase(me, true);
}

static void node_status_end_phase(node_status_ao_t *me) {
  if (me->mode != NODE_STATUS_MODE_BLINK) {
    return;
  }
  node_status_begin_phase(me, !me->lit);
}

static void node_status_begin_phase(node_status_ao_t *me, bool lit) {
  me->lit = lit;
  node_status_log_phase(lit);
  node_status_drive(me, lit);
  /* Phase times are uint16_t, far below the arm limit of INT32_MAX, and the
   * timer is initialised at start, so arming cannot fail. */
  (void)node_ao_time_evt_arm(&me->phase_timer, lit ? me->on_ms : me->off_ms, 0);
}

static void node_status_drive(node_status_ao_t *me, bool lit) {
  const int level = (lit == NODE_HAL_BOARD_STATUS_LED_ACTIVE_HIGH) ? 1 : 0;
  const node_hal_err_t err =
      node_hal_gpio_write(NODE_HAL_BOARD_STATUS_LED_GPIO, level);
  if (err != NODE_HAL_OK) {
    if (!me->write_failing) {
      me->write_failing = true;
      node_status_log_write_failed(NODE_HAL_BOARD_STATUS_LED_GPIO, err);
    }
    return;
  }
  if (me->write_failing) {
    me->write_failing = false;
    node_status_log_write_recovered(NODE_HAL_BOARD_STATUS_LED_GPIO);
  }
}

static node_status_err_t node_status_post(const void *evt, size_t size) {
  switch (node_ao_post(&g_status.super, evt, size)) {
    case NODE_AO_OK:
      return NODE_STATUS_OK;
    case NODE_AO_ERR_FULL:
      return NODE_STATUS_ERR_FULL;
    case NODE_AO_ERR_STATE:
      return NODE_STATUS_ERR_STATE;
    default:
      return NODE_STATUS_ERR_ARG;
  }
}
