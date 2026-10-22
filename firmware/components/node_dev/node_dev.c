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
} node_dev_instance_t;

/** @brief The devices active object. */
typedef struct {
  node_ao_t super;          /**< Framework part; must be first. */
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
 * @brief Initialise one device with its driver; on failure, log it and put
 *        the device in FAULT.
 *
 * @param me        The devices AO.
 * @param dev_index Device's position in node_config_t::dev.
 */
static void node_dev_init_device(node_dev_ao_t *me, int dev_index);

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
      .evt_size = sizeof(node_ao_evt_t),
  };
  g_devices.cfg = cfg;
  if (node_ao_start(&g_devices.super, node_dev_dispatch, &ao_cfg) !=
      NODE_AO_OK) {
    return NODE_DEV_ERR_STATE;
  }
  g_started = true;
  return NODE_DEV_OK;
}

node_dev_err_t node_dev_set_onoff(int dev_index, bool on) {
  (void)on;
  if (!node_dev_index_in_range(dev_index)) {
    return NODE_DEV_ERR_ARG;
  }
  /* TODO(chunk 8): post the command to the devices AO. */
  return NODE_DEV_ERR_STATE;
}

node_dev_err_t node_dev_post_from_isr(int dev_index) {
  if (!node_dev_index_in_range(dev_index)) {
    return NODE_DEV_ERR_ARG;
  }
  /* TODO(chunk 8): post the ISR event to the devices AO. */
  return NODE_DEV_ERR_STATE;
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
    default:
      break;
  }
}

static void node_dev_init_all(node_dev_ao_t *me) {
  const int n_i2c =
      node_dev_clamp_count(me->cfg->n_i2c, NODE_CONFIG_MAX_I2C_BUSES);
  for (int i = 0; i < n_i2c; i++) {
    node_dev_init_bus(me, i);
  }
  const int n_dev =
      node_dev_clamp_count(me->cfg->n_dev, NODE_CONFIG_MAX_DEVICES);
  for (int i = 0; i < n_dev; i++) {
    node_dev_init_device(me, i);
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

static void node_dev_init_device(node_dev_ao_t *me, int dev_index) {
  node_dev_instance_t *dev = &me->devs[dev_index];
  const node_config_dev_t *cfg = &me->cfg->dev[dev_index];

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
    args.bus = me->buses[bus_index];
    if (args.bus == NULL) {
      node_dev_log_warning("[%d] '%s': its I2C bus is not initialised",
                           dev_index, cfg->id);
      node_dev_set_fault(me, dev_index, true);
      return;
    }
  }

  (void)memset(&dev->sched, 0, sizeof(dev->sched));
  void *state = NULL;
  const node_dev_err_t err = dev->drv->init(&args, &state, &dev->sched);
  if (err != NODE_DEV_OK) {
    node_dev_log_warning("[%d] '%s': init failed: %d", dev_index, cfg->id,
                         (int)err);
    node_dev_set_fault(me, dev_index, true);
    return;
  }
  dev->state = state;
  dev->initialised = true;
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
