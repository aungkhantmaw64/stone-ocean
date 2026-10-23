#pragma once

/**
 * @file node_health_port_mock.h
 * @brief Test controls for the host implementation of node_health_port.c.
 *
 * The fake keeps the crash counter in memory instead of NVS, lets a test
 * choose the reset reason and the outcome of NVS initialisation, and can
 * make counter reads and writes fail. Call node_health_port_mock_reset()
 * from every test's setUp() so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_health_port.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Return to a working, empty NVS: reason unknown, no counter stored,
 *        NVS initialisation succeeds, no forced failures.
 */
void node_health_port_mock_reset(void);

/**
 * @brief Set the reason node_health_port_reset_reason() reports.
 *
 * @param reason Reason to report.
 */
void node_health_port_mock_set_reset_reason(node_health_port_reason_t reason);

/**
 * @brief Set what node_health_port_nvs_init() reports before and after the
 *        partition is erased.
 *
 * @param before_erase Outcome until node_health_port_nvs_erase() runs.
 * @param after_erase  Outcome once it has run.
 */
void node_health_port_mock_set_nvs_init(node_health_port_nvs_t before_erase,
                                        node_health_port_nvs_t after_erase);

/**
 * @brief Make node_health_port_nvs_erase() fail, until the next reset.
 */
void node_health_port_mock_fail_nvs_erase(void);

/**
 * @brief Number of times the NVS partition was erased since the last reset.
 *
 * @return The count.
 */
size_t node_health_port_mock_nvs_erase_count(void);

/**
 * @brief Store a crash counter, as if left by an earlier boot.
 *
 * @param count Count to store.
 */
void node_health_port_mock_set_counter(uint32_t count);

/**
 * @brief Whether a crash counter is stored.
 *
 * @return True if one is stored.
 */
bool node_health_port_mock_counter_is_stored(void);

/**
 * @brief The stored crash counter.
 *
 * @return The count, or 0 if none is stored.
 */
uint32_t node_health_port_mock_counter(void);

/**
 * @brief Make every later counter read fail, until the next reset.
 */
void node_health_port_mock_fail_counter_load(void);

/**
 * @brief Make every later counter write fail, until the next reset.
 */
void node_health_port_mock_fail_counter_store(void);

/**
 * @brief Make every later erase of the `health` namespace fail, until the
 *        next reset.
 */
void node_health_port_mock_fail_health_erase(void);

/**
 * @brief Number of times the node was restarted since the last reset.
 *
 * @return The count.
 */
size_t node_health_port_mock_restart_count(void);
