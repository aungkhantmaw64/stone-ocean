#pragma once

/**
 * @file node_health_internal.h
 * @brief Shared between node_health's source files, plus hooks for host
 *        tests. Not part of the public interface.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_health.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Start the health AO, which watches the reset button.
 *
 * Called once by node_health_init(), from task context. In its init event
 * the AO sets the button's pin up as an input with an edge interrupt; if
 * that fails, it logs a warning and the button does nothing.
 *
 * @return NODE_HEALTH_OK, or NODE_HEALTH_ERR_STATE if it is already started
 *         or could not be started.
 */
node_health_err_t node_health_ao_start(void);

/**
 * @brief Forget that the health AO was started and clear its state.
 *
 * Host tests only, after node_ao_port_mock_reset(), while no thread uses
 * the health AO.
 */
void node_health_ao_reset(void);

/**
 * @brief Forget node_health_init(), the reset reason, the crash count and
 *        every registered hook. Does not touch the health AO.
 *
 * Host tests only.
 */
void node_health_reset(void);
