/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "node_ao.h"
#include "node_ao_pubsub.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One (AO, signal) subscription. */
typedef struct {
  node_ao_t *ao;     /**< Subscribed AO; valid once @c ready is set. */
  node_ao_sig_t sig; /**< Published signal; valid once @c ready is set. */
  atomic_uint ready; /**< 1 once @c ao and @c sig are written, else 0.
                        Written with release order, read with acquire order,
                        so a publisher never sees a half-written entry. */
} node_ao_pubsub_entry_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Subscriptions, in the order they were made. Append-only: an entry
 *         is claimed, written, then marked ready, so publishers in other
 *         threads can scan it without a lock. */
static node_ao_pubsub_entry_t
    g_subscriptions[NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS] = {0};

/** @brief Entries of @c g_subscriptions claimed so far, 0 to
 *         NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS. Claimed by compare-and-swap, so
 *         threads that subscribe at once each get their own entry. */
static atomic_size_t g_n_claimed = 0;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Whether @p sig is in the published signal range.
 *
 * @param sig Signal to check.
 * @return True if @p sig is at least NODE_AO_SIG_PUB_FIRST.
 */
static bool node_ao_pubsub_is_published(node_ao_sig_t sig);

/**
 * @brief Whether a ready entry already subscribes @p me to @p sig.
 *
 * @param me  AO to look for.
 * @param sig Published signal to look for.
 * @return True if the subscription exists.
 */
static bool node_ao_pubsub_exists(const node_ao_t *me, node_ao_sig_t sig);

/**
 * @brief Claim the next free entry of the subscription table.
 *
 * @param index Receives the claimed index; must not be NULL.
 * @return True if an entry was claimed, false if the table is full.
 */
static bool node_ao_pubsub_claim(size_t *index);

/**
 * @brief Number of entries claimed so far, capped at the table size.
 *
 * @return 0 to NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS.
 */
static size_t node_ao_pubsub_claimed(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_ao_err_t node_ao_pubsub_subscribe(node_ao_t *me, node_ao_sig_t sig) {
  if (me == NULL || !node_ao_pubsub_is_published(sig)) {
    return NODE_AO_ERR_ARG;
  }
  /* Reliable when each AO's subscriptions are made from one thread at a
   * time, which is how components subscribe: in their start function or
   * their own dispatch. */
  if (node_ao_pubsub_exists(me, sig)) {
    return NODE_AO_OK;
  }
  size_t index = 0;
  if (!node_ao_pubsub_claim(&index)) {
    return NODE_AO_ERR_FULL;
  }
  node_ao_pubsub_entry_t *entry = &g_subscriptions[index];
  entry->ao = me;
  entry->sig = sig;
  atomic_store_explicit(&entry->ready, 1U, memory_order_release);
  return NODE_AO_OK;
}

node_ao_err_t node_ao_pubsub_publish(const void *evt, size_t size) {
  if (evt == NULL || size < sizeof(node_ao_evt_t) ||
      size > NODE_AO_MAX_EVT_SIZE) {
    return NODE_AO_ERR_ARG;
  }
  const node_ao_sig_t sig = ((const node_ao_evt_t *)evt)->sig;
  if (!node_ao_pubsub_is_published(sig)) {
    return NODE_AO_ERR_ARG;
  }

  node_ao_err_t result = NODE_AO_OK;
  const size_t n_claimed = node_ao_pubsub_claimed();
  for (size_t i = 0; i < n_claimed; i++) {
    node_ao_pubsub_entry_t *entry = &g_subscriptions[i];
    if (atomic_load_explicit(&entry->ready, memory_order_acquire) == 0U ||
        entry->sig != sig) {
      continue;
    }
    const node_ao_err_t err = node_ao_post(entry->ao, evt, size);
    /* NODE_AO_ERR_STATE means the subscriber is not started yet, which the
     * spec treats as nobody to deliver to, not as a failure. */
    if (err != NODE_AO_OK && err != NODE_AO_ERR_STATE && result == NODE_AO_OK) {
      result = err;
    }
  }
  return result;
}

void node_ao_pubsub_reset(void) {
  for (size_t i = 0; i < NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS; i++) {
    atomic_store_explicit(&g_subscriptions[i].ready, 0U, memory_order_relaxed);
    g_subscriptions[i].ao = NULL;
    g_subscriptions[i].sig = 0;
  }
  atomic_store_explicit(&g_n_claimed, 0U, memory_order_relaxed);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_ao_pubsub_is_published(node_ao_sig_t sig) {
  return sig >= NODE_AO_SIG_PUB_FIRST;
}

static bool node_ao_pubsub_exists(const node_ao_t *me, node_ao_sig_t sig) {
  const size_t n_claimed = node_ao_pubsub_claimed();
  for (size_t i = 0; i < n_claimed; i++) {
    const node_ao_pubsub_entry_t *entry = &g_subscriptions[i];
    if (atomic_load_explicit(&entry->ready, memory_order_acquire) != 0U &&
        entry->ao == me && entry->sig == sig) {
      return true;
    }
  }
  return false;
}

static bool node_ao_pubsub_claim(size_t *index) {
  size_t claimed = atomic_load_explicit(&g_n_claimed, memory_order_relaxed);
  /* Each failed swap means another thread claimed an entry, so the count
   * only grows and the table is full after at most this many attempts. */
  for (size_t attempt = 0; attempt <= NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS;
       attempt++) {
    if (claimed >= NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(
            &g_n_claimed, &claimed, claimed + 1U, memory_order_relaxed,
            memory_order_relaxed)) {
      *index = claimed;
      return true;
    }
  }
  return false;
}

static size_t node_ao_pubsub_claimed(void) {
  const size_t claimed =
      atomic_load_explicit(&g_n_claimed, memory_order_relaxed);
  return (claimed < NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS)
             ? claimed
             : NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS;
}
