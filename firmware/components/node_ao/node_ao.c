/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "node_ao.h"
#include "node_ao_port.h"
#include "node_ao_time.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One queue slot, aligned so it can be cast to any AO's event
 *         struct. */
typedef union {
  node_ao_evt_t evt;                         /**< Header of the event. */
  unsigned char bytes[NODE_AO_MAX_EVT_SIZE]; /**< Raw slot contents. */
  max_align_t align; /**< Forces the strictest alignment. */
} node_ao_slot_t;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Whether @p cfg describes a queue and thread the core can run.
 *
 * The port checks what it alone knows, such as the highest priority.
 *
 * @param cfg Config to check; NULL is invalid.
 * @return True if the name, queue length, event size and stack size are
 *         usable.
 */
static bool node_ao_config_is_valid(const node_ao_config_t *cfg);

/**
 * @brief Check a post and copy the event into a zeroed queue slot.
 *
 * @param me   Target AO.
 * @param evt  Event to post.
 * @param size Size of @p evt in bytes.
 * @param slot Receives the event, zero-padded; must not be NULL.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG or NODE_AO_ERR_STATE.
 */
static node_ao_err_t node_ao_prepare_post(const node_ao_t *me, const void *evt,
                                          size_t size, node_ao_slot_t *slot);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_ao_err_t node_ao_start(node_ao_t *me, node_ao_dispatch_t dispatch,
                            const node_ao_config_t *cfg) {
  if (me == NULL || dispatch == NULL || !node_ao_config_is_valid(cfg)) {
    return NODE_AO_ERR_ARG;
  }
  if (me->started) {
    return NODE_AO_ERR_STATE;
  }
  me->dispatch = dispatch;
  me->cfg = *cfg;

  node_ao_err_t err = node_ao_port_queue_create(me);
  if (err != NODE_AO_OK) {
    return err;
  }
  node_ao_slot_t init = {0};
  init.evt.sig = NODE_AO_SIG_INIT;
  err = node_ao_port_send(me, &init);
  if (err != NODE_AO_OK) {
    node_ao_port_queue_delete(me);
    return err;
  }

  /* Marked started before the thread exists, so the init dispatch can
   * already post to its own AO. */
  me->started = true;
  err = node_ao_port_thread_create(me);
  if (err != NODE_AO_OK) {
    me->started = false;
    node_ao_port_queue_delete(me);
  }
  return err;
}

node_ao_err_t node_ao_post(node_ao_t *me, const void *evt, size_t size) {
  node_ao_slot_t slot = {0};
  node_ao_err_t err = node_ao_prepare_post(me, evt, size, &slot);
  if (err != NODE_AO_OK) {
    return err;
  }
  return node_ao_port_send(me, &slot);
}

node_ao_err_t node_ao_post_from_isr(node_ao_t *me, const void *evt,
                                    size_t size) {
  node_ao_slot_t slot = {0};
  node_ao_err_t err = node_ao_prepare_post(me, evt, size, &slot);
  if (err != NODE_AO_OK) {
    return err;
  }
  return node_ao_port_send_from_isr(me, &slot);
}

size_t node_ao_run_once(node_ao_t *me) {
  if (me == NULL || !me->started) {
    return 0;
  }
  size_t dispatched = 0;
  node_ao_slot_t slot = {0};
  if (node_ao_port_receive(me, &slot, node_ao_time_next_wait_ms(me))) {
    me->dispatch(me, &slot.evt);
    dispatched++;
  }
  return dispatched + node_ao_time_dispatch_due(me);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_ao_config_is_valid(const node_ao_config_t *cfg) {
  return cfg != NULL && cfg->name != NULL && cfg->queue_len > 0 &&
         cfg->evt_size >= sizeof(node_ao_evt_t) &&
         cfg->evt_size <= NODE_AO_MAX_EVT_SIZE && cfg->stack_bytes > 0;
}

static node_ao_err_t node_ao_prepare_post(const node_ao_t *me, const void *evt,
                                          size_t size, node_ao_slot_t *slot) {
  if (me == NULL || evt == NULL) {
    return NODE_AO_ERR_ARG;
  }
  if (!me->started) {
    return NODE_AO_ERR_STATE;
  }
  if (size < sizeof(node_ao_evt_t) || size > me->cfg.evt_size) {
    return NODE_AO_ERR_ARG;
  }
  (void)memcpy(slot->bytes, evt, size);
  return NODE_AO_OK;
}
