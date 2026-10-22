#pragma once

/**
 * @file node_ao_port.h
 * @brief Interface between the portable node_ao core and a port. Not part of
 *        the public interface.
 *
 * The core (node_ao.c, node_ao_time.c) validates arguments and dispatches
 * events. A port owns the queue and the thread of each AO. The FreeRTOS port
 * is node_ao_port.c; the host port in firmware/test/host/mocks/ runs AOs
 * synchronously. Every queue slot is me->cfg.evt_size bytes.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_ao.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Create the AO's queue from me->cfg and store its handle in
 *        me->queue.
 *
 * Called by node_ao_start() from task context, after the config is checked.
 *
 * @param me AO being started; must not be NULL.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG if the port cannot hold that many
 *         events, or NODE_AO_ERR_NO_MEMORY.
 */
node_ao_err_t node_ao_port_queue_create(node_ao_t *me);

/**
 * @brief Delete the AO's queue and clear me->queue.
 *
 * Called by node_ao_start() when the thread cannot be created.
 *
 * @param me AO whose queue was created; must not be NULL.
 */
void node_ao_port_queue_delete(node_ao_t *me);

/**
 * @brief Create the AO's thread from me->cfg and store its handle in
 *        me->thread. The thread calls node_ao_run_once() forever.
 *
 * Called by node_ao_start() from task context, after the queue is created
 * and the init event is queued.
 *
 * @param me AO being started; must not be NULL.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG if the port cannot use that priority
 *         or stack size, or NODE_AO_ERR_NO_MEMORY.
 */
node_ao_err_t node_ao_port_thread_create(node_ao_t *me);

/**
 * @brief Copy one queue slot to the back of the AO's queue. Never blocks.
 *
 * Task context only.
 *
 * @param me   AO with a queue; must not be NULL.
 * @param slot me->cfg.evt_size bytes to queue; must not be NULL.
 * @return NODE_AO_OK, or NODE_AO_ERR_FULL if the queue is full.
 */
node_ao_err_t node_ao_port_send(node_ao_t *me, const void *slot);

/**
 * @brief node_ao_port_send() for ISR context.
 *
 * Requests a context switch at the end of the ISR if the AO's thread has a
 * higher priority than the interrupted task.
 *
 * @param me   AO with a queue; must not be NULL.
 * @param slot me->cfg.evt_size bytes to queue; must not be NULL.
 * @return NODE_AO_OK, or NODE_AO_ERR_FULL if the queue is full.
 */
node_ao_err_t node_ao_port_send_from_isr(node_ao_t *me, const void *slot);

/**
 * @brief Take the event at the front of the AO's queue.
 *
 * Called only from the AO's own thread. The FreeRTOS port blocks for up to
 * @p timeout_ms; the host port never blocks.
 *
 * @param me         AO with a queue; must not be NULL.
 * @param slot       Receives me->cfg.evt_size bytes; must not be NULL.
 * @param timeout_ms Longest time to wait for an event, in ms.
 * @return True if an event was taken, false if the queue stayed empty.
 */
bool node_ao_port_receive(node_ao_t *me, void *slot, uint32_t timeout_ms);

/**
 * @brief One turn of the AO loop: wait for an event, dispatch it, then
 *        dispatch the time events that are due.
 *
 * Implemented by the core in node_ao.c and called only by the port, from
 * the AO's own thread.
 *
 * @param me Started AO; must not be NULL.
 * @return The number of events dispatched, time events included.
 */
size_t node_ao_run_once(node_ao_t *me);
