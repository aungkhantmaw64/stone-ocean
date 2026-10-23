#pragma once

/**
 * @file node_dev_mock.h
 * @brief Test controls for the host fake of node_dev.c used by the
 *        node_mesh tests.
 *
 * The fake only provides node_dev_set_onoff(): it records each command
 * instead of posting it to the devices AO. Tests of node_dev itself link the
 * real node_dev.c. Call node_dev_mock_reset() from every test's setUp() so
 * tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>

#include "node_dev.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Forget every recorded command, and accept commands again. */
void node_dev_mock_reset(void);

/**
 * @brief Make every later node_dev_set_onoff() return @p err without
 *        recording the command, or NODE_DEV_OK to record them again.
 *
 * @param err Result to return.
 */
void node_dev_mock_set_onoff_result(node_dev_err_t err);

/**
 * @brief Number of on/off commands recorded since the last reset.
 *
 * @return The count.
 */
size_t node_dev_mock_onoff_count(void);

/**
 * @brief Device index of the last on/off command recorded.
 *
 * @return The index; -1 if none was recorded.
 */
int node_dev_mock_last_onoff_dev(void);

/**
 * @brief State asked for by the last on/off command recorded.
 *
 * @return True for on; false for off, or if none was recorded.
 */
bool node_dev_mock_last_onoff_on(void);
