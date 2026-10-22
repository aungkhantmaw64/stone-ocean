/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_ao_port.h"
#include "node_ao_port_mock.h"
#include "node_ao_pubsub.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief A queue of one started AO, kept as a ring of fixed-size slots. */
typedef struct {
  node_ao_t *owner; /**< AO the queue belongs to; NULL if the entry is free. */
  bool running;     /**< True once the AO's thread was "created". */
  size_t len;       /**< Capacity in events, from the AO's config. */
  size_t slot_size; /**< Bytes per slot, the AO's evt_size. */
  size_t head;      /**< Index of the oldest queued slot. */
  size_t count;     /**< Number of queued slots, 0 to @c len. */
  unsigned char slots[NODE_AO_PORT_MOCK_MAX_QUEUE_LEN]
                     [NODE_AO_MAX_EVT_SIZE]; /**< Slot storage. */
} node_ao_port_mock_queue_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Queues of the started AOs, in the order they were created. */
static node_ao_port_mock_queue_t g_queues[NODE_AO_PORT_MOCK_MAX_AOS] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Zero an AO and the time events it owns.
 *
 * @param ao AO to clear; must not be NULL.
 */
static void node_ao_port_mock_clear_ao(node_ao_t *ao);

/**
 * @brief Run each running AO once, in creation order.
 *
 * @return The number of events dispatched in this round.
 */
static size_t node_ao_port_mock_run_round(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_ao_port_mock_reset(void) {
  for (size_t i = 0; i < NODE_AO_PORT_MOCK_MAX_AOS; i++) {
    if (g_queues[i].owner != NULL) {
      node_ao_port_mock_clear_ao(g_queues[i].owner);
    }
  }
  (void)memset(g_queues, 0, sizeof(g_queues));
  node_ao_pubsub_reset();
}

int node_ao_port_mock_run_until_idle(void) {
  size_t total = 0;
  while (total <= NODE_AO_PORT_MOCK_MAX_DISPATCHES) {
    size_t dispatched = node_ao_port_mock_run_round();
    if (dispatched == 0) {
      return (int)total;
    }
    total += dispatched;
  }
  return NODE_AO_PORT_MOCK_RUNAWAY;
}

node_ao_err_t node_ao_port_queue_create(node_ao_t *me) {
  if (me->cfg.queue_len > NODE_AO_PORT_MOCK_MAX_QUEUE_LEN) {
    return NODE_AO_ERR_ARG;
  }
  for (size_t i = 0; i < NODE_AO_PORT_MOCK_MAX_AOS; i++) {
    node_ao_port_mock_queue_t *queue = &g_queues[i];
    if (queue->owner == NULL) {
      *queue = (node_ao_port_mock_queue_t){
          .owner = me,
          .len = me->cfg.queue_len,
          .slot_size = me->cfg.evt_size,
      };
      me->queue = queue;
      return NODE_AO_OK;
    }
  }
  return NODE_AO_ERR_NO_MEMORY;
}

void node_ao_port_queue_delete(node_ao_t *me) {
  node_ao_port_mock_queue_t *queue = me->queue;
  if (queue != NULL) {
    *queue = (node_ao_port_mock_queue_t){0};
    me->queue = NULL;
  }
}

node_ao_err_t node_ao_port_thread_create(node_ao_t *me) {
  node_ao_port_mock_queue_t *queue = me->queue;
  queue->running = true;
  me->thread = queue;
  return NODE_AO_OK;
}

node_ao_err_t node_ao_port_send(node_ao_t *me, const void *slot) {
  node_ao_port_mock_queue_t *queue = me->queue;
  if (queue->count >= queue->len) {
    return NODE_AO_ERR_FULL;
  }
  size_t tail = (queue->head + queue->count) % queue->len;
  (void)memcpy(queue->slots[tail], slot, queue->slot_size);
  queue->count++;
  return NODE_AO_OK;
}

node_ao_err_t node_ao_port_send_from_isr(node_ao_t *me, const void *slot) {
  return node_ao_port_send(me, slot);
}

/* Never blocks: the host port has no threads, so waiting would only hang
 * the test. */
bool node_ao_port_receive(node_ao_t *me, void *slot, uint32_t timeout_ms) {
  (void)timeout_ms;
  node_ao_port_mock_queue_t *queue = me->queue;
  if (queue->count == 0) {
    return false;
  }
  (void)memcpy(slot, queue->slots[queue->head], queue->slot_size);
  queue->head = (queue->head + 1) % queue->len;
  queue->count--;
  return true;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_ao_port_mock_clear_ao(node_ao_t *ao) {
  for (size_t i = 0; i < ao->n_time_evts && i < NODE_AO_MAX_TIME_EVTS; i++) {
    if (ao->time_evts[i] != NULL) {
      *ao->time_evts[i] = (node_ao_time_evt_t){0};
    }
  }
  *ao = (node_ao_t){0};
}

static size_t node_ao_port_mock_run_round(void) {
  size_t dispatched = 0;
  for (size_t i = 0; i < NODE_AO_PORT_MOCK_MAX_AOS; i++) {
    if (g_queues[i].running) {
      dispatched += node_ao_run_once(g_queues[i].owner);
    }
  }
  return dispatched;
}
