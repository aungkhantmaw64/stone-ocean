/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_ao.h"
#include "node_ao_time.h"
#include "node_hal.h"

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Signed time from @p now until @p deadline_ms, correct across the
 *        32-bit millisecond wrap-around.
 *
 * @param deadline_ms Deadline, in HAL ms.
 * @param now_ms      Current HAL time, in ms.
 * @return Milliseconds until the deadline; zero or negative once it is due.
 */
static int32_t node_ao_time_until(uint32_t deadline_ms, uint32_t now_ms);

/**
 * @brief Disarm a one-shot time event that fired, or move a periodic one to
 *        its next deadline without letting it fall behind.
 *
 * @param te     Time event that is due; must not be NULL.
 * @param now_ms Current HAL time, in ms.
 */
static void node_ao_time_reschedule(node_ao_time_evt_t *te, uint32_t now_ms);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_ao_err_t node_ao_time_evt_init(node_ao_time_evt_t *te, node_ao_t *owner,
                                    node_ao_sig_t sig) {
  if (te == NULL || owner == NULL || sig < NODE_AO_SIG_USER) {
    return NODE_AO_ERR_ARG;
  }
  if (te->owner != NULL) {
    return NODE_AO_ERR_STATE;
  }
  if (owner->n_time_evts >= NODE_AO_MAX_TIME_EVTS) {
    return NODE_AO_ERR_FULL;
  }
  *te = (node_ao_time_evt_t){.owner = owner, .sig = sig};
  owner->time_evts[owner->n_time_evts] = te;
  owner->n_time_evts++;
  return NODE_AO_OK;
}

node_ao_err_t node_ao_time_evt_arm(node_ao_time_evt_t *te, uint32_t delay_ms,
                                   uint32_t period_ms) {
  if (te == NULL || delay_ms > INT32_MAX || period_ms > INT32_MAX) {
    return NODE_AO_ERR_ARG;
  }
  if (te->owner == NULL) {
    return NODE_AO_ERR_STATE;
  }
  te->deadline_ms = node_hal_time_ms() + delay_ms;
  te->period_ms = period_ms;
  te->armed = true;
  return NODE_AO_OK;
}

node_ao_err_t node_ao_time_evt_disarm(node_ao_time_evt_t *te) {
  if (te == NULL) {
    return NODE_AO_ERR_ARG;
  }
  if (te->owner == NULL) {
    return NODE_AO_ERR_STATE;
  }
  te->armed = false;
  return NODE_AO_OK;
}

bool node_ao_time_evt_is_armed(const node_ao_time_evt_t *te) {
  return te != NULL && te->armed;
}

uint32_t node_ao_time_next_wait_ms(const node_ao_t *me) {
  uint32_t now_ms = node_hal_time_ms();
  uint32_t wait_ms = NODE_AO_MAX_WAIT_MS;
  for (size_t i = 0; i < me->n_time_evts && i < NODE_AO_MAX_TIME_EVTS; i++) {
    const node_ao_time_evt_t *te = me->time_evts[i];
    if (!te->armed) {
      continue;
    }
    int32_t until_ms = node_ao_time_until(te->deadline_ms, now_ms);
    if (until_ms <= 0) {
      return 0;
    }
    if ((uint32_t)until_ms < wait_ms) {
      wait_ms = (uint32_t)until_ms;
    }
  }
  return wait_ms;
}

size_t node_ao_time_dispatch_due(node_ao_t *me) {
  uint32_t now_ms = node_hal_time_ms();
  size_t dispatched = 0;
  for (size_t i = 0; i < me->n_time_evts && i < NODE_AO_MAX_TIME_EVTS; i++) {
    node_ao_time_evt_t *te = me->time_evts[i];
    if (!te->armed || node_ao_time_until(te->deadline_ms, now_ms) > 0) {
      continue;
    }
    node_ao_time_reschedule(te, now_ms);
    const node_ao_evt_t evt = {.sig = te->sig};
    me->dispatch(me, &evt);
    dispatched++;
  }
  return dispatched;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

/* Deadlines are at most INT32_MAX ms ahead (checked when arming), so the
 * difference always fits and its sign says which time comes first. */
static int32_t node_ao_time_until(uint32_t deadline_ms, uint32_t now_ms) {
  return (int32_t)(deadline_ms - now_ms);
}

static void node_ao_time_reschedule(node_ao_time_evt_t *te, uint32_t now_ms) {
  if (te->period_ms == 0) {
    te->armed = false;
    return;
  }
  te->deadline_ms += te->period_ms;
  if (node_ao_time_until(te->deadline_ms, now_ms) <= 0) {
    te->deadline_ms = now_ms + te->period_ms;
  }
}
