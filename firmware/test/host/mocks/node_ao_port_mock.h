#pragma once

/**
 * @file node_ao_port_mock.h
 * @brief Test controls for the host implementation of node_ao_port.c.
 *
 * The host port creates no threads. Started AOs and their queues live in
 * static memory, and tests run them synchronously with
 * node_ao_port_mock_run_until_idle(), on the mock HAL clock. Call
 * node_ao_port_mock_reset() from every test's setUp() so tests stay
 * independent.
 */

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Most AOs the host port runs at once. */
#define NODE_AO_PORT_MOCK_MAX_AOS 8

/** @brief Longest queue the host port can create, in events. */
#define NODE_AO_PORT_MOCK_MAX_QUEUE_LEN 16

/** @brief Most events node_ao_port_mock_run_until_idle() dispatches in one
 *         call before it gives up. */
#define NODE_AO_PORT_MOCK_MAX_DISPATCHES 1000

/** @brief Returned by node_ao_port_mock_run_until_idle() when the AOs were
 *         still busy after NODE_AO_PORT_MOCK_MAX_DISPATCHES events. */
#define NODE_AO_PORT_MOCK_RUNAWAY (-1)

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Forget every started AO, its queued events and every subscription.
 *
 * Zeroes each AO started since the last reset, and the time events it owns,
 * so tests can start them again.
 */
void node_ao_port_mock_reset(void);

/**
 * @brief Run every started AO until none has work left.
 *
 * Goes round the AOs in the order they were started, dispatching their
 * queued events and their time events that are due at the mock HAL time.
 * Time does not advance during the call; advance it with
 * node_hal_time_mock_advance() between calls.
 *
 * @return The number of events dispatched, time events included, or
 *         NODE_AO_PORT_MOCK_RUNAWAY if the AOs were still busy after
 *         NODE_AO_PORT_MOCK_MAX_DISPATCHES events.
 */
int node_ao_port_mock_run_until_idle(void);
