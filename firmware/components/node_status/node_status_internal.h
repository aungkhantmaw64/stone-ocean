#pragma once

/**
 * @file node_status_internal.h
 * @brief Hooks of the status AO for host tests. Not part of the public
 *        interface.
 */

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Forget that the status AO was started and clear its state.
 *
 * Host tests only, after node_ao_port_mock_reset(), while no thread uses
 * the status AO.
 */
void node_status_reset(void);
