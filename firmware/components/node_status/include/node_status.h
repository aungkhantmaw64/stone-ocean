#pragma once

/**
 * @file node_status.h
 * @brief The board's status LED, owned by the status AO (spec 16).
 *
 * The status LED is part of the board (node_hal_board.h), not of the config.
 * It publishes no events and is not a node_dev device, so the mesh never sees
 * it. Callers switch it on, off or blinking with commands that are posted to
 * the status AO; the latest command wins.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Shortest on or off time node_status_led_blink() accepts, in ms. */
#define NODE_STATUS_BLINK_MIN_MS 20

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

/**
 * @brief Blink the status LED, starting with the on phase.
 *
 * Replaces whatever the LED was doing; a new blink restarts the pattern with
 * the new times. Each phase may run late by the status AO's dispatch latency
 * (a few ms). Safe from any task; never blocks. Not for ISR context.
 *
 * @param on_ms  Time the LED is on in each period, in ms; at least
 *               NODE_STATUS_BLINK_MIN_MS.
 * @param off_ms Time the LED is off in each period, in ms; at least
 *               NODE_STATUS_BLINK_MIN_MS.
 * @return NODE_STATUS_OK once the command is queued, NODE_STATUS_ERR_ARG for
 *         a time below NODE_STATUS_BLINK_MIN_MS, NODE_STATUS_ERR_STATE before
 *         node_status_start(), or NODE_STATUS_ERR_FULL if the status AO's
 *         queue is full.
 */
node_status_err_t node_status_led_blink(uint16_t on_ms, uint16_t off_ms);

#ifdef __cplusplus
}
#endif
