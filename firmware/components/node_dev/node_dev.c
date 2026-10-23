/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_config.h"
#include "node_dev_internal.h"
#include "node_dev_log.h"
#include "node_dev_report.h"
#include "node_hal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Most drivers that can be registered, one per device type. */
#define NODE_DEV_MAX_DRIVERS 8

/** @brief Name of the devices AO's thread, also used in logs. */
#define NODE_DEV_AO_NAME "node_dev"

/** @brief FreeRTOS priority of the devices AO; below the Bluedroid host
 *         tasks, which run near configMAX_PRIORITIES. */
#define NODE_DEV_AO_PRIORITY 5

/** @brief Stack size of the devices AO's thread, in bytes (spec 06: at most
 *         4 KB). */
#define NODE_DEV_AO_STACK_BYTES 4096

/** @brief Events the devices AO's queue holds. */
#define NODE_DEV_AO_QUEUE_LEN 16

/** @brief Value of a device's bus index when it is not on an I2C bus. */
#define NODE_DEV_NO_BUS (-1)

/** @brief Longest delay the devices AO schedules, in ms; the limit of
 *         node_ao_time_evt_arm(). */
#define NODE_DEV_MAX_DELAY_MS ((uint32_t)INT32_MAX)

_Static_assert(sizeof(node_dev_evt_t) <= NODE_AO_MAX_EVT_SIZE,
               "node_dev_evt_t must fit in an AO queue slot");

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One configured device, owned by the devices AO. */
typedef struct {
  const node_dev_driver_t *drv;   /**< Driver of the device's type; NULL if
                                     none was registered. */
  void *state;                    /**< Driver state from init; valid while
                                     @c initialised. */
  node_dev_sched_t sched;         /**< Schedule from the driver's init. */
  bool initialised;               /**< True once the driver's init
                                     succeeded. */
  bool fault;                     /**< True while the device is in FAULT. */
  node_dev_report_state_t report; /**< What was last published for the
                                     device. */
  bool sample_scheduled;          /**< True while a call to the driver's
                                     sample is due at @c sample_at_ms. */
  uint32_t sample_at_ms;          /**< HAL time of the next sample call, in
                                     ms; valid while @c sample_scheduled. */
  bool sample_in_progress;        /**< True between a sample that returned
                                     NODE_DEV_PENDING and the call that
                                     finishes it. */
  uint32_t cycle_start_ms;        /**< HAL time the current or last sample
                                     started, in ms; the next periodic
                                     sample is due one interval later. */
  uint8_t sample_fails;           /**< Failed samples in a row, 0 to
                                     NODE_DEV_MAX_SAMPLE_FAILS. */
  bool retry_scheduled;           /**< True while an init retry is due at
                                     @c retry_at_ms. */
  uint32_t retry_at_ms;           /**< HAL time of the next init retry, in
                                     ms; valid while @c retry_scheduled. */
} node_dev_instance_t;

/** @brief Private signals of the devices AO. */
enum {
  NODE_DEV_SIG_TIMER = NODE_AO_SIG_USER, /**< The earliest scheduled
                                            deadline is due. */
  NODE_DEV_SIG_SET_ONOFF,                /**< Command from
                                            node_dev_set_onoff(). */
  NODE_DEV_SIG_ISR,                      /**< A device's interrupt fired;
                                            from node_dev_post_from_isr(). */
};

/** @brief Command to switch an actuator, posted by node_dev_set_onoff(). */
typedef struct {
  node_ao_evt_t super; /**< Signal NODE_DEV_SIG_SET_ONOFF. */
  uint8_t dev_index;   /**< Device's position in node_config_t::dev, 0 to
                          NODE_CONFIG_MAX_DEVICES - 1. */
  bool on;             /**< True to switch on. */
} node_dev_onoff_evt_t;

/** @brief A device's interrupt, posted by node_dev_post_from_isr(). */
typedef struct {
  node_ao_evt_t super; /**< Signal NODE_DEV_SIG_ISR. */
  uint8_t dev_index;   /**< Device's position in node_config_t::dev, 0 to
                          NODE_CONFIG_MAX_DEVICES - 1. */
} node_dev_isr_evt_t;

_Static_assert(sizeof(node_dev_isr_evt_t) <= sizeof(node_dev_onoff_evt_t),
               "the devices AO's evt_size must hold its largest event");

/** @brief The devices active object. */
typedef struct {
  node_ao_t super;          /**< Framework part; must be first. */
  node_ao_time_evt_t timer; /**< One-shot, armed to the earliest scheduled
                               deadline of any device. */
  const node_config_t *cfg; /**< Config the devices come from; set by
                               node_dev_start(). */
  node_hal_i2c_bus_t buses[NODE_CONFIG_MAX_I2C_BUSES]; /**< Initialised I2C
                                                          buses, by config
                                                          index; NULL if
                                                          init failed. */
  node_dev_instance_t devs[NODE_CONFIG_MAX_DEVICES];   /**< Devices, by
                                                          config index. */
} node_dev_ao_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Registered drivers, in registration order; written only before
 *         node_dev_start(), from task context. */
static const node_dev_driver_t *g_drivers[NODE_DEV_MAX_DRIVERS] = {0};

/** @brief Number of valid entries in g_drivers. */
static size_t g_n_drivers = 0;

/** @brief True once node_dev_start() started the devices AO. */
static bool g_started = false;

/** @brief The devices AO; apart from @c cfg, set before start, its state is
 *         touched only in its own thread. */
static node_dev_ao_t g_devices = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle one event of the devices AO.
 *
 * @param me The devices AO.
 * @param e  The event.
 */
static void node_dev_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Initialise the I2C buses, then the devices, in config order, and log
 *        the device table.
 *
 * @param me The devices AO.
 */
static void node_dev_init_all(node_dev_ao_t *me);

/**
 * @brief Initialise one I2C bus of the config; on failure, log it and leave
 *        its handle NULL.
 *
 * @param me        The devices AO.
 * @param bus_index Bus's position in node_config_t::i2c.
 */
static void node_dev_init_bus(node_dev_ao_t *me, int bus_index);

/**
 * @brief Initialise one device with its driver, or retry a failed init.
 *
 * On success, a device in FAULT leaves it and its first sample is
 * scheduled. On failure, the device goes to FAULT and, unless the failure is
 * permanent (no driver, no report policy), a retry is scheduled
 * NODE_DEV_INIT_RETRY_MS later.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param now_ms    Current HAL time, in ms.
 * @param retry     True for a retry, which re-initialises a failed I2C bus
 *                  first.
 */
static void node_dev_init_device(node_dev_ao_t *me, int dev_index,
                                 uint32_t now_ms, bool retry);

/**
 * @brief Put a device whose init failed in FAULT and schedule its retry.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param now_ms    Current HAL time, in ms.
 */
static void node_dev_init_failed(node_dev_ao_t *me, int dev_index,
                                 uint32_t now_ms);

/**
 * @brief Call a device's sample method; publish the value if it is due and
 *        schedule the next call.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param now_ms    Current HAL time, in ms.
 */
static void node_dev_sample_device(node_dev_ao_t *me, int dev_index,
                                   uint32_t now_ms);

/**
 * @brief Count a failed sample; the NODE_DEV_MAX_SAMPLE_FAILS-th in a row
 *        puts the device in FAULT.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param err       The driver's error.
 */
static void node_dev_sample_failed(node_dev_ao_t *me, int dev_index,
                                   node_dev_err_t err);

/**
 * @brief Publish a successful sample if the device's report rules say so.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param value     The sample.
 * @param now_ms    Current HAL time, in ms.
 */
static void node_dev_publish_if_due(node_dev_ao_t *me, int dev_index,
                                    const node_dev_value_t *value,
                                    uint32_t now_ms);

/**
 * @brief Schedule a device's next periodic sample, one interval after the
 *        last one started, or one interval from now if that has passed.
 *
 * @param dev    The device; nothing is scheduled if its interval is 0.
 * @param now_ms Current HAL time, in ms.
 */
static void node_dev_schedule_periodic(node_dev_instance_t *dev,
                                       uint32_t now_ms);

/**
 * @brief Schedule a call to a device's sample method.
 *
 * @param dev      The device.
 * @param now_ms   Current HAL time, in ms.
 * @param delay_ms Time until the call, in ms; capped at
 *                 NODE_DEV_MAX_DELAY_MS.
 */
static void node_dev_schedule_sample(node_dev_instance_t *dev, uint32_t now_ms,
                                     uint32_t delay_ms);

/**
 * @brief Run every device whose scheduled sample or init retry is due.
 *
 * @param me     The devices AO.
 * @param now_ms Current HAL time, in ms.
 */
static void node_dev_run_due(node_dev_ao_t *me, uint32_t now_ms);

/**
 * @brief Arm the timer to the earliest scheduled deadline of any device, or
 *        disarm it if none is scheduled.
 *
 * @param me     The devices AO.
 * @param now_ms Current HAL time, in ms.
 */
static void node_dev_arm_timer(node_dev_ao_t *me, uint32_t now_ms);

/**
 * @brief Time left until a deadline; handles the HAL clock's wrap-around.
 *
 * @param at_ms  Deadline, in ms.
 * @param now_ms Current HAL time, in ms.
 * @return Time until @p at_ms, in ms; 0 if it has been reached.
 */
static uint32_t node_dev_delay_until(uint32_t at_ms, uint32_t now_ms);

/**
 * @brief Whether a deadline has been reached; handles the HAL clock's
 *        wrap-around.
 *
 * @param at_ms  Deadline, in ms.
 * @param now_ms Current HAL time, in ms.
 * @return True if @p at_ms is not after @p now_ms.
 */
static bool node_dev_is_due(uint32_t at_ms, uint32_t now_ms);

/**
 * @brief Index of the I2C bus a device uses.
 *
 * @param dev The device's config entry.
 * @return Its position in node_config_t::i2c, or NODE_DEV_NO_BUS for a GPIO
 *         device.
 */
static int node_dev_bus_index(const node_config_dev_t *dev);

/**
 * @brief Registered driver of a device type.
 *
 * @param type Device type.
 * @return The driver, or NULL if none is registered.
 */
static const node_dev_driver_t *node_dev_find_driver(
    node_config_dev_type_t type);

/**
 * @brief Whether a signal is one a driver may publish its samples with.
 *
 * @param sig Signal to check.
 * @return True for NODE_DEV_SIG_READING, _MOTION and _ONOFF_CHANGED.
 */
static bool node_dev_is_sample_sig(node_ao_sig_t sig);

/**
 * @brief Put a device in or out of FAULT; on a change, log it and publish
 *        NODE_DEV_SIG_FAULT.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param fault     True to enter FAULT.
 */
static void node_dev_set_fault(node_dev_ao_t *me, int dev_index, bool fault);

/**
 * @brief Publish a device event; log it and any subscriber that missed it.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 * @param sig       One of NODE_DEV_SIG_*.
 * @param fault     For NODE_DEV_SIG_FAULT, the new state; false otherwise.
 * @param value     The device's value; NULL publishes zero.
 */
static void node_dev_publish(const node_dev_ao_t *me, int dev_index,
                             node_ao_sig_t sig, bool fault,
                             const node_dev_value_t *value);

/**
 * @brief Switch an actuator on or off for a node_dev_set_onoff() command;
 *        publish NODE_DEV_SIG_ONOFF_CHANGED if its state changed.
 *
 * @param me  The devices AO.
 * @param cmd The command.
 */
static void node_dev_handle_onoff(node_dev_ao_t *me,
                                  const node_dev_onoff_evt_t *cmd);

/**
 * @brief Pass a device's interrupt to its driver and schedule the sample the
 *        driver asks for, restarting any earlier delay.
 *
 * @param me     The devices AO.
 * @param isr    The interrupt event.
 * @param now_ms Current HAL time, in ms.
 */
static void node_dev_handle_isr(node_dev_ao_t *me,
                                const node_dev_isr_evt_t *isr, uint32_t now_ms);

/**
 * @brief Translate a node_ao post result into a node_dev result.
 *
 * @param err Result of node_ao_post() or node_ao_post_from_isr().
 * @return The matching node_dev_err_t.
 */
static node_dev_err_t node_dev_from_ao_err(node_ao_err_t err);

/**
 * @brief Whether a device index can name a device of any config.
 *
 * @param dev_index Index to check.
 * @return True if @p dev_index is 0 to NODE_CONFIG_MAX_DEVICES - 1.
 */
static bool node_dev_index_in_range(int dev_index);

/**
 * @brief Clamp a config count to 0 .. @p max.
 *
 * @param count Count from the config.
 * @param max   Largest allowed count.
 * @return The clamped count.
 */
static int node_dev_clamp_count(int count, int max);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_dev_err_t node_dev_register_driver(const node_dev_driver_t *drv) {
  if ((drv == NULL) || (drv->init == NULL) ||
      !node_dev_is_sample_sig(drv->sample_sig)) {
    return NODE_DEV_ERR_ARG;
  }
  if (g_started) {
    return NODE_DEV_ERR_STATE;
  }
  for (size_t i = 0; i < g_n_drivers; i++) {
    if (g_drivers[i]->type == drv->type) {
      g_drivers[i] = drv;
      return NODE_DEV_OK;
    }
  }
  if (g_n_drivers >= NODE_DEV_MAX_DRIVERS) {
    return NODE_DEV_ERR_FULL;
  }
  g_drivers[g_n_drivers] = drv;
  g_n_drivers++;
  return NODE_DEV_OK;
}

node_dev_err_t node_dev_start(const node_config_t *cfg) {
  if (cfg == NULL) {
    return NODE_DEV_ERR_ARG;
  }
  if (g_started) {
    return NODE_DEV_ERR_STATE;
  }
  const node_ao_config_t ao_cfg = {
      .name = NODE_DEV_AO_NAME,
      .priority = NODE_DEV_AO_PRIORITY,
      .stack_bytes = NODE_DEV_AO_STACK_BYTES,
      .queue_len = NODE_DEV_AO_QUEUE_LEN,
      .evt_size = sizeof(node_dev_onoff_evt_t),
  };
  g_devices.cfg = cfg;
  if (node_ao_time_evt_init(&g_devices.timer, &g_devices.super,
                            NODE_DEV_SIG_TIMER) != NODE_AO_OK) {
    return NODE_DEV_ERR_STATE;
  }
  if (node_ao_start(&g_devices.super, node_dev_dispatch, &ao_cfg) !=
      NODE_AO_OK) {
    return NODE_DEV_ERR_STATE;
  }
  g_started = true;
  return NODE_DEV_OK;
}

node_dev_err_t node_dev_set_onoff(int dev_index, bool on) {
  if (!node_dev_index_in_range(dev_index)) {
    return NODE_DEV_ERR_ARG;
  }
  const node_dev_onoff_evt_t cmd = {
      .super = {.sig = NODE_DEV_SIG_SET_ONOFF},
      .dev_index = (uint8_t)dev_index,
      .on = on,
  };
  return node_dev_from_ao_err(
      node_ao_post(&g_devices.super, &cmd, sizeof(cmd)));
}

node_dev_err_t node_dev_post_from_isr(int dev_index) {
  if (!node_dev_index_in_range(dev_index)) {
    return NODE_DEV_ERR_ARG;
  }
  const node_dev_isr_evt_t isr = {
      .super = {.sig = NODE_DEV_SIG_ISR},
      .dev_index = (uint8_t)dev_index,
  };
  return node_dev_from_ao_err(
      node_ao_post_from_isr(&g_devices.super, &isr, sizeof(isr)));
}

void node_dev_reset(void) {
  (void)memset(g_drivers, 0, sizeof(g_drivers));
  g_n_drivers = 0;
  g_started = false;
  (void)memset(&g_devices, 0, sizeof(g_devices));
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_dev_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  node_dev_ao_t *self = (node_dev_ao_t *)me;
  switch (e->sig) {
    case NODE_AO_SIG_INIT:
      node_dev_init_all(self);
      break;
    case NODE_DEV_SIG_TIMER:
      node_dev_run_due(self, node_hal_time_ms());
      break;
    case NODE_DEV_SIG_SET_ONOFF:
      node_dev_handle_onoff(self, (const node_dev_onoff_evt_t *)e);
      break;
    case NODE_DEV_SIG_ISR:
      node_dev_handle_isr(self, (const node_dev_isr_evt_t *)e,
                          node_hal_time_ms());
      break;
    default:
      break;
  }
  node_dev_arm_timer(self, node_hal_time_ms());
}

static void node_dev_init_all(node_dev_ao_t *me) {
  const int n_i2c =
      node_dev_clamp_count(me->cfg->n_i2c, NODE_CONFIG_MAX_I2C_BUSES);
  for (int i = 0; i < n_i2c; i++) {
    node_dev_init_bus(me, i);
  }
  const int n_dev =
      node_dev_clamp_count(me->cfg->n_dev, NODE_CONFIG_MAX_DEVICES);
  const uint32_t now_ms = node_hal_time_ms();
  for (int i = 0; i < n_dev; i++) {
    node_dev_init_device(me, i, now_ms, false);
  }
  node_dev_log_table_title(n_dev);
  for (int i = 0; i < n_dev; i++) {
    node_dev_log_table_row(i, &me->cfg->dev[i], me->devs[i].fault);
  }
}

static void node_dev_init_bus(node_dev_ao_t *me, int bus_index) {
  const node_config_i2c_bus_t *bus = &me->cfg->i2c[bus_index];
  const node_hal_err_t err = node_hal_i2c_bus_init(
      bus->port, bus->sda, bus->scl, bus->freq_hz, &me->buses[bus_index]);
  if (err != NODE_HAL_OK) {
    me->buses[bus_index] = NULL;
    node_dev_log_warning("I2C bus '%s' init failed: %d", bus->id, (int)err);
  }
}

static void node_dev_init_device(node_dev_ao_t *me, int dev_index,
                                 uint32_t now_ms, bool retry) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  const node_config_dev_t *cfg = &me->cfg->dev[dev_index];
  dev->retry_scheduled = false;

  dev->drv = node_dev_find_driver(cfg->type);
  if (dev->drv == NULL) {
    node_dev_log_warning("[%d] '%s': no driver for its type", dev_index,
                         cfg->id);
    node_dev_set_fault(me, dev_index, true);
    return;
  }

  node_dev_init_args_t args = {.dev_index = dev_index, .cfg = cfg};
  const int bus_index = node_dev_bus_index(cfg);
  if (bus_index != NODE_DEV_NO_BUS) {
    if (retry && (me->buses[bus_index] == NULL)) {
      node_dev_init_bus(me, bus_index);
    }
    args.bus = me->buses[bus_index];
    if (args.bus == NULL) {
      node_dev_log_warning("[%d] '%s': its I2C bus is not initialised",
                           dev_index, cfg->id);
      node_dev_init_failed(me, dev_index, now_ms);
      return;
    }
  }

  (void)memset(&dev->sched, 0, sizeof(dev->sched));
  void *state = NULL;
  const node_dev_err_t err = dev->drv->init(&args, &state, &dev->sched);
  if (err != NODE_DEV_OK) {
    node_dev_log_warning("[%d] '%s': init failed: %d", dev_index, cfg->id,
                         (int)err);
    node_dev_init_failed(me, dev_index, now_ms);
    return;
  }
  if ((dev->drv->sample_sig == NODE_DEV_SIG_READING) &&
      (dev->sched.report == NULL)) {
    node_dev_log_warning("[%d] '%s': driver gave no report policy", dev_index,
                         cfg->id);
    node_dev_set_fault(me, dev_index, true);
    return;
  }
  dev->state = state;
  dev->initialised = true;
  node_dev_set_fault(me, dev_index, false);
  if (dev->drv->sample != NULL) {
    /* init may wait briefly (spec 06), so time the first sample from when it
     * returned; a stale now_ms would shorten the sample's first wait. */
    node_dev_schedule_sample(dev, node_hal_time_ms(), 0);
  }
}

static void node_dev_init_failed(node_dev_ao_t *me, int dev_index,
                                 uint32_t now_ms) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  node_dev_set_fault(me, dev_index, true);
  dev->retry_at_ms = now_ms + NODE_DEV_INIT_RETRY_MS;
  dev->retry_scheduled = true;
}

static void node_dev_sample_device(node_dev_ao_t *me, int dev_index,
                                   uint32_t now_ms) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  dev->sample_scheduled = false;
  if (!dev->sample_in_progress) {
    dev->cycle_start_ms = now_ms;
  }

  node_dev_value_t value = {0};
  uint32_t again_ms = 0;
  const node_dev_err_t err = dev->drv->sample(dev->state, &value, &again_ms);
  if (err == NODE_DEV_PENDING) {
    dev->sample_in_progress = true;
    node_dev_schedule_sample(dev, now_ms, again_ms);
    return;
  }
  dev->sample_in_progress = false;

  if (err == NODE_DEV_OK) {
    dev->sample_fails = 0;
    node_dev_set_fault(me, dev_index, false);
    node_dev_publish_if_due(me, dev_index, &value, now_ms);
  } else {
    node_dev_sample_failed(me, dev_index, err);
  }
  node_dev_schedule_periodic(dev, now_ms);
}

static void node_dev_sample_failed(node_dev_ao_t *me, int dev_index,
                                   node_dev_err_t err) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  node_dev_log_warning("[%d] '%s': sample failed: %d", dev_index,
                       me->cfg->dev[dev_index].id, (int)err);
  if (dev->sample_fails < NODE_DEV_MAX_SAMPLE_FAILS) {
    dev->sample_fails++;
  }
  if (dev->sample_fails >= NODE_DEV_MAX_SAMPLE_FAILS) {
    node_dev_set_fault(me, dev_index, true);
  }
}

static void node_dev_publish_if_due(node_dev_ao_t *me, int dev_index,
                                    const node_dev_value_t *value,
                                    uint32_t now_ms) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  if (!node_dev_report_is_due(&dev->report, dev->drv->sample_sig,
                              dev->sched.report, value, now_ms)) {
    return;
  }
  node_dev_publish(me, dev_index, dev->drv->sample_sig, false, value);
  node_dev_report_record(&dev->report, value, now_ms);
}

static void node_dev_schedule_periodic(node_dev_instance_t *dev,
                                       uint32_t now_ms) {
  const uint32_t interval_ms = dev->sched.sample_interval_ms;
  if (interval_ms == 0) {
    return;
  }
  const uint32_t elapsed_ms = now_ms - dev->cycle_start_ms;
  const uint32_t delay_ms =
      (elapsed_ms < interval_ms) ? (interval_ms - elapsed_ms) : interval_ms;
  node_dev_schedule_sample(dev, now_ms, delay_ms);
}

static void node_dev_schedule_sample(node_dev_instance_t *dev, uint32_t now_ms,
                                     uint32_t delay_ms) {
  const uint32_t capped_ms =
      (delay_ms > NODE_DEV_MAX_DELAY_MS) ? NODE_DEV_MAX_DELAY_MS : delay_ms;
  dev->sample_at_ms = now_ms + capped_ms;
  dev->sample_scheduled = true;
}

static void node_dev_run_due(node_dev_ao_t *me, uint32_t now_ms) {
  const int n_dev =
      node_dev_clamp_count(me->cfg->n_dev, NODE_CONFIG_MAX_DEVICES);
  for (int i = 0; i < n_dev; i++) {
    const node_dev_instance_t *dev = &me->devs[i];
    if (dev->retry_scheduled && node_dev_is_due(dev->retry_at_ms, now_ms)) {
      node_dev_init_device(me, i, now_ms, true);
    }
    if (dev->sample_scheduled && node_dev_is_due(dev->sample_at_ms, now_ms)) {
      node_dev_sample_device(me, i, now_ms);
    }
  }
}

static void node_dev_arm_timer(node_dev_ao_t *me, uint32_t now_ms) {
  bool any = false;
  uint32_t earliest_delay_ms = NODE_DEV_MAX_DELAY_MS;
  for (int i = 0; i < NODE_CONFIG_MAX_DEVICES; i++) {
    const node_dev_instance_t *dev = &me->devs[i];
    if (dev->sample_scheduled) {
      any = true;
      const uint32_t delay_ms = node_dev_delay_until(dev->sample_at_ms, now_ms);
      if (delay_ms < earliest_delay_ms) {
        earliest_delay_ms = delay_ms;
      }
    }
    if (dev->retry_scheduled) {
      any = true;
      const uint32_t delay_ms = node_dev_delay_until(dev->retry_at_ms, now_ms);
      if (delay_ms < earliest_delay_ms) {
        earliest_delay_ms = delay_ms;
      }
    }
  }
  /* Arming and disarming fail only for an uninitialised timer, which
   * node_dev_start() rules out. */
  if (any) {
    (void)node_ao_time_evt_arm(&me->timer, earliest_delay_ms, 0);
  } else {
    (void)node_ao_time_evt_disarm(&me->timer);
  }
}

static uint32_t node_dev_delay_until(uint32_t at_ms, uint32_t now_ms) {
  return node_dev_is_due(at_ms, now_ms) ? 0 : (at_ms - now_ms);
}

static bool node_dev_is_due(uint32_t at_ms, uint32_t now_ms) {
  return (int32_t)(now_ms - at_ms) >= 0;
}

static int node_dev_bus_index(const node_config_dev_t *dev) {
  switch (dev->type) {
    case NODE_CONFIG_DEV_SHTC3:
      if ((dev->shtc3.bus_index < 0) ||
          (dev->shtc3.bus_index >= NODE_CONFIG_MAX_I2C_BUSES)) {
        return NODE_DEV_NO_BUS;
      }
      return dev->shtc3.bus_index;
    case NODE_CONFIG_DEV_PIR:
    case NODE_CONFIG_DEV_LED:
    default:
      return NODE_DEV_NO_BUS;
  }
}

static const node_dev_driver_t *node_dev_find_driver(
    node_config_dev_type_t type) {
  for (size_t i = 0; i < g_n_drivers; i++) {
    if (g_drivers[i]->type == type) {
      return g_drivers[i];
    }
  }
  return NULL;
}

static bool node_dev_is_sample_sig(node_ao_sig_t sig) {
  switch (sig) {
    case NODE_DEV_SIG_READING:
    case NODE_DEV_SIG_MOTION:
    case NODE_DEV_SIG_ONOFF_CHANGED:
      return true;
    default:
      return false;
  }
}

static void node_dev_set_fault(node_dev_ao_t *me, int dev_index, bool fault) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  if (dev->fault == fault) {
    return;
  }
  dev->fault = fault;
  node_dev_log_state_change(dev_index, me->cfg->dev[dev_index].id, fault);
  node_dev_publish(me, dev_index, NODE_DEV_SIG_FAULT, fault, NULL);
}

static void node_dev_publish(const node_dev_ao_t *me, int dev_index,
                             node_ao_sig_t sig, bool fault,
                             const node_dev_value_t *value) {
  node_dev_evt_t evt = {
      .super = {.sig = sig},
      .dev_index = (uint8_t)dev_index,
      .fault = fault,
      .ts_ms = node_hal_time_ms(),
  };
  if (value != NULL) {
    evt.value = *value;
  }
  const node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  if (err != NODE_AO_OK) {
    node_dev_log_warning("[%d] event %u not delivered to every subscriber: %d",
                         dev_index, (unsigned)sig, (int)err);
  }
  if (sig != NODE_DEV_SIG_FAULT) {
    node_dev_log_event(me->cfg->dev[dev_index].id, &evt);
  }
}

static void node_dev_handle_onoff(node_dev_ao_t *me,
                                  const node_dev_onoff_evt_t *cmd) {
  const int dev_index = cmd->dev_index;
  if (dev_index >=
      node_dev_clamp_count(me->cfg->n_dev, NODE_CONFIG_MAX_DEVICES)) {
    node_dev_log_warning("[%d] on/off ignored: no such device", dev_index);
    return;
  }
  node_dev_instance_t *dev = &me->devs[dev_index];
  const char *const id = me->cfg->dev[dev_index].id;
  if ((dev->drv == NULL) || (dev->drv->set_onoff == NULL)) {
    node_dev_log_warning("[%d] '%s': on/off ignored: not an actuator",
                         dev_index, id);
    return;
  }
  if (!dev->initialised || dev->fault) {
    node_dev_log_warning("[%d] '%s': on/off ignored: device in FAULT",
                         dev_index, id);
    return;
  }

  const node_dev_err_t err = dev->drv->set_onoff(dev->state, cmd->on);
  if (err != NODE_DEV_OK) {
    node_dev_log_warning("[%d] '%s': switching %s failed: %d", dev_index, id,
                         cmd->on ? "on" : "off", (int)err);
    return;
  }
  const uint32_t now_ms = node_hal_time_ms();
  node_dev_value_t value = dev->report.value;
  value.on = cmd->on;
  if (node_dev_report_is_due(&dev->report, NODE_DEV_SIG_ONOFF_CHANGED, NULL,
                             &value, now_ms)) {
    node_dev_publish(me, dev_index, NODE_DEV_SIG_ONOFF_CHANGED, false, &value);
    node_dev_report_record(&dev->report, &value, now_ms);
  }
}

static void node_dev_handle_isr(node_dev_ao_t *me,
                                const node_dev_isr_evt_t *isr,
                                uint32_t now_ms) {
  const int dev_index = isr->dev_index;
  if (dev_index >=
      node_dev_clamp_count(me->cfg->n_dev, NODE_CONFIG_MAX_DEVICES)) {
    node_dev_log_warning("[%d] interrupt ignored: no such device", dev_index);
    return;
  }
  node_dev_instance_t *dev = &me->devs[dev_index];
  if (!dev->initialised || (dev->drv->on_isr == NULL)) {
    /* An interrupt that races a failed init, or a driver without on_isr,
     * has nothing to do. */
    return;
  }
  const uint32_t delay_ms = dev->drv->on_isr(dev->state);
  if (dev->drv->sample != NULL) {
    node_dev_schedule_sample(dev, now_ms, delay_ms);
  }
}

static node_dev_err_t node_dev_from_ao_err(node_ao_err_t err) {
  switch (err) {
    case NODE_AO_OK:
      return NODE_DEV_OK;
    case NODE_AO_ERR_FULL:
      return NODE_DEV_ERR_FULL;
    case NODE_AO_ERR_STATE:
      return NODE_DEV_ERR_STATE;
    case NODE_AO_ERR_ARG:
    case NODE_AO_ERR_NO_MEMORY:
    default:
      return NODE_DEV_ERR_ARG;
  }
}

static bool node_dev_index_in_range(int dev_index) {
  return (dev_index >= 0) && (dev_index < NODE_CONFIG_MAX_DEVICES);
}

static int node_dev_clamp_count(int count, int max) {
  if (count < 0) {
    return 0;
  }
  if (count > max) {
    return max;
  }
  return count;
}
