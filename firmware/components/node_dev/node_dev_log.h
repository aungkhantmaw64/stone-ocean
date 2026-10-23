#pragma once

/**
 * @file node_dev_log.h
 * @brief Log lines of the devices AO. Not part of the public interface.
 *
 * Logs with ESP_LOG under the tag "node_dev" on the target. The host mock
 * records what was logged so tests can check it. Call from the devices AO's
 * thread only.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>

#include "node_config.h"
#include "node_dev.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Log the title of the device table at INFO level.
 *
 * @param n_dev Number of configured devices.
 */
void node_dev_log_table_title(int n_dev);

/**
 * @brief Log one row of the device table at INFO level: index, id, type and
 *        state.
 *
 * @param dev_index Device's position in node_config_t::dev.
 * @param dev       The device's config entry; NULL is ignored.
 * @param fault     True if the device is in FAULT.
 */
void node_dev_log_table_row(int dev_index, const node_config_dev_t *dev,
                            bool fault);

/**
 * @brief Log that a device entered or left FAULT, at WARNING level when
 *        entering and INFO level when leaving.
 *
 * @param dev_index Device's position in node_config_t::dev.
 * @param id        NUL-terminated device id; NULL is logged as "".
 * @param fault     True if the device entered FAULT.
 */
void node_dev_log_state_change(int dev_index, const char *id, bool fault);

/**
 * @brief Log a published reading, motion change or on/off change at INFO
 *        level. Fault events are logged by node_dev_log_state_change().
 *
 * @param id  NUL-terminated device id; NULL is logged as "".
 * @param evt The published event; NULL is ignored.
 */
void node_dev_log_event(const char *id, const node_dev_evt_t *evt);

/**
 * @brief Log a fact about a device at INFO level, for example a sensor's
 *        serial number read at init.
 *
 * @param fmt printf-style format; NULL is ignored.
 * @param ... Format arguments.
 */
void node_dev_log_info(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a problem at WARNING level, for example a failed init or a
 *        dropped command.
 *
 * @param fmt printf-style format; NULL is ignored.
 * @param ... Format arguments.
 */
void node_dev_log_warning(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
