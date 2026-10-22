#pragma once

/**
 * @file node_dev.h
 * @brief Device driver framework: the devices active object.
 *
 * One active object, the devices AO, owns every configured device. It
 * initialises the drivers from the node config, samples them on a schedule
 * and publishes what changes with node_ao_pubsub_publish(): readings, motion,
 * on/off changes and faults (NODE_DEV_SIG_*). Components that need device
 * data subscribe to those signals and keep their own copy; nobody reads the
 * devices AO's state. Switching an actuator is a command posted to the
 * devices AO with node_dev_set_onoff().
 *
 * A device's index is its position in node_config_t::dev.
 *
 * Driver methods run in the devices AO's thread and must not block: no delays
 * and no waiting on queues or semaphores. A short HAL bus transfer is
 * allowed. This header includes no FreeRTOS or ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "node_ao.h"
#include "node_ao_pub_sigs.h"
#include "node_config.h"
#include "node_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Time between init retries of a device in FAULT, in ms. */
#define NODE_DEV_INIT_RETRY_MS 30000

/** @brief Failed samples in a row that put a device in FAULT. */
#define NODE_DEV_MAX_SAMPLE_FAILS 3

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a node_dev call or a driver method. */
typedef enum {
  NODE_DEV_OK = 0,    /**< Success. */
  NODE_DEV_ERR_ARG,   /**< NULL pointer or bad argument from the caller. */
  NODE_DEV_ERR_STATE, /**< The devices AO is not started, or already
                         started. */
  NODE_DEV_ERR_FULL,  /**< The driver registry or the devices AO's queue is
                         full; a posted command is dropped. */
  NODE_DEV_ERR_IO,    /**< The device or its bus failed. */
  NODE_DEV_PENDING,   /**< Returned by a driver's sample: a measurement is
                         running; call sample again after *again_ms. */
} node_dev_err_t;

/** @brief Signals the devices AO publishes. Subscribe to them with
 *         node_ao_pubsub_subscribe(); each event is a node_dev_evt_t. */
enum {
  NODE_DEV_SIG_READING = NODE_AO_PUB_SIGS_NODE_DEV, /**< Sensor reading,
                                                       throttled by the
                                                       device's report
                                                       policy. */
  NODE_DEV_SIG_MOTION,        /**< Motion started or stopped; value.motion
                                 holds the new state. */
  NODE_DEV_SIG_ONOFF_CHANGED, /**< Actuator switched on or off; value.on
                                 holds the new state. */
  NODE_DEV_SIG_FAULT,         /**< Device entered or left FAULT; fault holds
                                 the new state. */
};

/** @brief Latest value of a device. Only the members of its type are set. */
typedef struct {
  float temp_c; /**< SHTC3 temperature, in °C. */
  float rh_pct; /**< SHTC3 relative humidity, in percent, 0 to 100. */
  bool motion;  /**< PIR: true while motion is sensed. */
  bool on;      /**< LED: true while switched on. */
} node_dev_value_t;

/** @brief Every event the devices AO publishes. Fits in
 *         NODE_AO_MAX_EVT_SIZE. */
typedef struct {
  node_ao_evt_t super;    /**< Signal: one of NODE_DEV_SIG_*. */
  uint8_t dev_index;      /**< Device's position in node_config_t::dev, 0 to
                             NODE_CONFIG_MAX_DEVICES - 1. */
  bool fault;             /**< NODE_DEV_SIG_FAULT: true if the device entered
                             FAULT, false if it left it. False otherwise. */
  node_dev_value_t value; /**< The device's value; zero for
                             NODE_DEV_SIG_FAULT. */
  uint32_t ts_ms;         /**< node_hal_time_ms() when the event was
                             published, in ms. */
} node_dev_evt_t;

/** @brief What a driver's init gets to know about its device. */
typedef struct {
  int dev_index;                /**< Device's position in node_config_t::dev. */
  const node_config_dev_t *cfg; /**< The device's config entry; valid while
                                   the devices AO runs. */
  node_hal_i2c_bus_t bus;       /**< The device's initialised I2C bus; NULL
                                   for GPIO devices. */
} node_dev_init_args_t;

/** @brief How the framework schedules and reports a device. Filled in by its
 *         driver's init, zeroed beforehand. */
typedef struct {
  uint32_t sample_interval_ms;        /**< Time between samples, in ms; 0 if the
                                         device is not sampled periodically. */
  const node_config_report_t *report; /**< Report policy of a
                                         NODE_DEV_SIG_READING driver, valid
                                         while the devices AO runs; NULL for
                                         other drivers. */
} node_dev_sched_t;

/** @brief A driver: the methods the devices AO calls for one device type.
 *         Allocate statically; all methods run in the devices AO's thread
 *         and must not block. */
typedef struct node_dev_driver {
  node_config_dev_type_t type; /**< Device type this driver handles. */
  node_ao_sig_t sample_sig;    /**< Signal published for a sample:
                                  NODE_DEV_SIG_READING (throttled by the
                                  report policy), NODE_DEV_SIG_MOTION or
                                  NODE_DEV_SIG_ONOFF_CHANGED (published when
                                  the value changes). */

  /**
   * @brief Set up one device.
   *
   * @param args  The device's index, config and bus; never NULL.
   * @param state Receives the driver's state for this device, passed back to
   *              the other methods. Drivers keep it in static storage.
   * @param sched Zeroed; receives the device's schedule.
   * @return NODE_DEV_OK, or an error; the device then goes to FAULT and init
   *         is retried every NODE_DEV_INIT_RETRY_MS.
   */
  node_dev_err_t (*init)(const node_dev_init_args_t *args, void **state,
                         node_dev_sched_t *sched);

  /**
   * @brief Read the device's current value. NULL for pure actuators.
   *
   * @param state    The state from init.
   * @param out      Receives the value on NODE_DEV_OK; zeroed beforehand.
   * @param again_ms On NODE_DEV_PENDING, receives the time in ms after which
   *                 sample is called again.
   * @return NODE_DEV_OK, NODE_DEV_PENDING, or an error, which counts towards
   *         NODE_DEV_MAX_SAMPLE_FAILS.
   */
  node_dev_err_t (*sample)(void *state, node_dev_value_t *out,
                           uint32_t *again_ms);

  /**
   * @brief Switch the device on or off. NULL for sensors.
   *
   * @param state The state from init.
   * @param on    True to switch on.
   * @return NODE_DEV_OK, or an error, which is logged.
   */
  node_dev_err_t (*set_onoff)(void *state, bool on);

  /**
   * @brief Handle an event the driver's ISR posted with
   *        node_dev_post_from_isr(). NULL if the driver has no ISR.
   *
   * @param state The state from init.
   * @return Time in ms after which the framework samples the device. A new
   *         ISR event before then restarts the delay.
   */
  uint32_t (*on_isr)(void *state);
} node_dev_driver_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Register the driver for one device type.
 *
 * Task context, before node_dev_start(). Registering a type again replaces
 * its driver.
 *
 * @param drv Driver; must not be NULL, must stay valid forever, must have an
 *            init method and a valid type and sample signal.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for a bad driver, or
 *         NODE_DEV_ERR_STATE after node_dev_start().
 */
node_dev_err_t node_dev_register_driver(const node_dev_driver_t *drv);

/**
 * @brief Start the devices AO, which initialises every device of @p cfg.
 *
 * Task context, once. Initialisation runs in the devices AO's thread: I2C
 * buses in config order, then devices in config order, then the device table
 * is logged.
 *
 * @param cfg Validated config; must not be NULL and must stay valid while
 *            the devices AO runs.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for a NULL config, or
 *         NODE_DEV_ERR_STATE if already started or the AO could not start.
 */
node_dev_err_t node_dev_start(const node_config_t *cfg);

/**
 * @brief Ask the devices AO to switch an actuator on or off. Never blocks.
 *
 * Safe from any task. The command is carried out in the devices AO, which
 * publishes NODE_DEV_SIG_ONOFF_CHANGED if the state changed. A device that
 * does not exist, is not an actuator or is in FAULT is logged and ignored.
 *
 * @param dev_index Device's position in node_config_t::dev, 0 to
 *                  NODE_CONFIG_MAX_DEVICES - 1.
 * @param on        True to switch on.
 * @return NODE_DEV_OK if the command was queued, NODE_DEV_ERR_ARG for an
 *         index out of range, NODE_DEV_ERR_STATE before node_dev_start(), or
 *         NODE_DEV_ERR_FULL if the queue is full and the command was dropped.
 */
node_dev_err_t node_dev_set_onoff(int dev_index, bool on);

/**
 * @brief Tell the devices AO that a device's interrupt fired. Never blocks.
 *
 * ISR context only. The devices AO then calls the driver's on_isr method in
 * its own thread.
 *
 * @param dev_index Device's position in node_config_t::dev, 0 to
 *                  NODE_CONFIG_MAX_DEVICES - 1.
 * @return As for node_dev_set_onoff().
 */
node_dev_err_t node_dev_post_from_isr(int dev_index);

#ifdef __cplusplus
}
#endif
