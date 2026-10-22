#pragma once

/**
 * @file node_dev_internal.h
 * @brief Test control of the devices AO. Not part of the public interface.
 */

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Forget the registered drivers, the devices and the started state.
 *
 * Host tests only, after node_ao_port_mock_reset(), while no thread uses
 * node_dev.
 */
void node_dev_reset(void);
