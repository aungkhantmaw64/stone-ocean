#pragma once

/**
 * @file node_mesh_port_mock.h
 * @brief Test controls for the host fake of node_mesh_port.c.
 *
 * The fake reports a fixed MAC address and free heap, and records restarts
 * instead of restarting. Call node_mesh_port_mock_reset() from every test's
 * setUp() so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_mesh_port.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded restart. */
void node_mesh_port_mock_reset(void);

/**
 * @brief Number of restarts since the last reset.
 *
 * @return The count.
 */
size_t node_mesh_port_mock_restart_count(void);
