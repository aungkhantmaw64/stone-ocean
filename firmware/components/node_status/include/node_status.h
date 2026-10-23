#pragma once

/**
 * @file node_status.h
 * @brief The board's status LED, owned by the status AO (spec 16).
 *
 * The status LED is part of the board (node_hal_board.h), not of the config.
 * It publishes no events and is not a node_dev device, so the mesh never sees
 * it. Callers switch it with commands that are posted to the status AO; the
 * latest command wins.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a node_status call. */
typedef enum {
  NODE_STATUS_OK = 0,    /**< Success. */
  NODE_STATUS_ERR_ARG,   /**< Bad argument from the caller. */
  NODE_STATUS_ERR_STATE, /**< The status AO is not started, is already
                            started, or could not be started. */
  NODE_STATUS_ERR_FULL,  /**< The status AO's queue is full; the command is
                            dropped. */
  NODE_STATUS_ERR_IO,    /**< The LED's pin could not be set up. The status
                            AO finds this in its own thread, logs it and
                            ignores later commands; no call returns it
                            today. */
} node_status_err_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Start the status AO.
 *
 * Call once, from task context, before the config is loaded. In its init
 * event the AO configures the LED's pin as an output and switches the LED
 * off. If the pin cannot be set up, the AO logs a warning and ignores every
 * later command; the node still boots.
 *
 * @return NODE_STATUS_OK, or NODE_STATUS_ERR_STATE if it is already started
 *         or the AO could not be started.
 */
node_status_err_t node_status_start(void);

/**
 * @brief Switch the status LED steadily on or off, ending any blinking.
 *
 * Safe from any task; never blocks. Not for ISR context.
 *
 * @param on True to switch the LED on, false to switch it off.
 * @return NODE_STATUS_OK once the command is queued, NODE_STATUS_ERR_STATE
 *         before node_status_start(), or NODE_STATUS_ERR_FULL if the status
 *         AO's queue is full.
 */
node_status_err_t node_status_led_set(bool on);

#ifdef __cplusplus
}
#endif
