#pragma once

/**
 * @file node_health_port.h
 * @brief ESP-IDF services node_health needs. Not part of the public
 *        interface.
 *
 * The ESP-IDF build links node_health_port.c; host tests link the fake in
 * firmware/test/host/mocks/, which keeps the crash counter in memory and
 * lets a test choose the reset reason. The port logs the ESP-IDF error of a
 * failed call itself; callers only see node_health_err_t. Task context
 * only.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>

#include "node_health.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Why the node last reset; one value per esp_reset_reason_t. */
typedef enum {
  NODE_HEALTH_PORT_REASON_UNKNOWN,   /**< Not known. */
  NODE_HEALTH_PORT_REASON_POWERON,   /**< Power-on. */
  NODE_HEALTH_PORT_REASON_EXT,       /**< External reset pin. */
  NODE_HEALTH_PORT_REASON_SW,        /**< esp_restart(). */
  NODE_HEALTH_PORT_REASON_PANIC,     /**< Panic or exception. */
  NODE_HEALTH_PORT_REASON_INT_WDT,   /**< Interrupt watchdog. */
  NODE_HEALTH_PORT_REASON_TASK_WDT,  /**< Task watchdog. */
  NODE_HEALTH_PORT_REASON_WDT,       /**< Any other watchdog. */
  NODE_HEALTH_PORT_REASON_DEEPSLEEP, /**< Wake from deep sleep. */
  NODE_HEALTH_PORT_REASON_BROWNOUT,  /**< Brownout. */
  NODE_HEALTH_PORT_REASON_SDIO,      /**< Reset over SDIO. */
  NODE_HEALTH_PORT_REASON_COUNT,     /**< Number of reasons; not a reason. */
} node_health_port_reason_t;

/** @brief Outcome of initialising NVS. */
typedef enum {
  NODE_HEALTH_PORT_NVS_OK,          /**< NVS is ready. */
  NODE_HEALTH_PORT_NVS_NO_FREE,     /**< The partition has no free pages;
                                       erasing it fixes this. */
  NODE_HEALTH_PORT_NVS_NEW_VERSION, /**< The partition holds a newer NVS
                                       format; erasing it fixes this. */
  NODE_HEALTH_PORT_NVS_FAILED,      /**< Any other failure. */
} node_health_port_nvs_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Initialise the default NVS partition.
 *
 * @return The outcome; see node_health_port_nvs_t.
 */
node_health_port_nvs_t node_health_port_nvs_init(void);

/**
 * @brief Erase the whole default NVS partition, mesh state included.
 *
 * @return NODE_HEALTH_OK, or NODE_HEALTH_ERR_IO.
 */
node_health_err_t node_health_port_nvs_erase(void);

/**
 * @brief Why the node last reset.
 *
 * @return The reason.
 */
node_health_port_reason_t node_health_port_reset_reason(void);

/**
 * @brief Read the crash counter from the `health` NVS namespace.
 *
 * @param out Receives the count; 0 if none is stored. Must not be NULL.
 * @return NODE_HEALTH_OK, also when none is stored; NODE_HEALTH_ERR_ARG
 *         for a NULL @p out, or NODE_HEALTH_ERR_IO.
 */
node_health_err_t node_health_port_counter_load(uint32_t *out);

/**
 * @brief Write the crash counter to the `health` NVS namespace and commit
 *        it.
 *
 * @param count Count to store.
 * @return NODE_HEALTH_OK, or NODE_HEALTH_ERR_IO.
 */
node_health_err_t node_health_port_counter_store(uint32_t count);

/**
 * @brief Erase every key in the `health` NVS namespace, the crash counter
 *        included, and commit.
 *
 * @return NODE_HEALTH_OK, also when the namespace does not exist; or
 *         NODE_HEALTH_ERR_IO.
 */
node_health_err_t node_health_port_health_erase(void);

/**
 * @brief Restart the node. Does not return on the target; the host fake
 *        records the restart and returns.
 */
void node_health_port_restart(void);
