#pragma once

/**
 * @file node_ao_time.h
 * @brief Time event scheduling used by the node_ao core loop. Not part of
 *        the public interface.
 *
 * Both functions run only in the owning AO's thread, which is also the only
 * place its time events are armed and disarmed, so no locking is needed.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_ao.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief How long the AO's thread may wait before its next time event is due.
 *
 * @param me AO whose time events to check; must not be NULL.
 * @return Milliseconds until the earliest armed time event is due, 0 if one
 *         is already due, capped at NODE_AO_MAX_WAIT_MS. NODE_AO_MAX_WAIT_MS
 *         if none is armed.
 */
uint32_t node_ao_time_next_wait_ms(const node_ao_t *me);

/**
 * @brief Dispatch every time event of @p me that is due, in the order they
 *        were initialised, and reschedule or disarm each one.
 *
 * @param me Started AO; must not be NULL.
 * @return The number of time events dispatched.
 */
size_t node_ao_time_dispatch_due(node_ao_t *me);
