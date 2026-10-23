#pragma once

/**
 * @file node_status_log.h
 * @brief Log lines of the status AO. Not part of the public interface.
 *
 * Logs with ESP_LOG under the tag "node_status" on the target. The host mock
 * records what was logged so tests can check it. Call from the status AO's
 * thread only.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>

#include "node_hal.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Log at INFO level that the status LED is set up and off.
 *
 * @param gpio GPIO number of the LED.
 */
void node_status_log_started(int gpio);

/**
 * @brief Log at WARNING level that the LED's pin could not be set up, so
 *        commands are ignored.
 *
 * @param gpio GPIO number of the LED.
 * @param err  The HAL's error.
 */
void node_status_log_setup_failed(int gpio, node_hal_err_t err);

/**
 * @brief Log at INFO level that the LED changed to steady on or off.
 *
 * @param on True for on.
 */
void node_status_log_steady(bool on);

/**
 * @brief Log at WARNING level that writes to the LED's pin started failing.
 *
 * @param gpio GPIO number of the LED.
 * @param err  The HAL's error.
 */
void node_status_log_write_failed(int gpio, node_hal_err_t err);

/**
 * @brief Log at INFO level that writes to the LED's pin work again.
 *
 * @param gpio GPIO number of the LED.
 */
void node_status_log_write_recovered(int gpio);
