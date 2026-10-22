#pragma once

/**
 * @file node_ao.h
 * @brief Active object framework: every firmware thread is an active object.
 *
 * An active object (AO) owns a private event queue and a thread that
 * dispatches one event at a time, to completion. A component embeds a
 * node_ao_t as the first member of its own AO struct, so its dispatch
 * function can cast @c me back to that struct. Other threads, ISRs and
 * ESP-IDF callbacks never touch an AO's state; they post events to it.
 *
 * Events are copied by value into the AO's queue. Every event struct starts
 * with a node_ao_evt_t header that holds its signal. Time events deliver a
 * plain node_ao_evt_t carrying their signal, in the owning AO's thread.
 *
 * Dispatch functions run in the AO's thread and must not block: no delays,
 * no blocking queue or semaphore calls. This header includes no FreeRTOS or
 * ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Largest event an AO can queue, in bytes, header included. */
#define NODE_AO_MAX_EVT_SIZE 32

/** @brief Most time events one AO can own. */
#define NODE_AO_MAX_TIME_EVTS 8

/** @brief Longest an AO's thread waits for its next event in one go, in ms,
 *         so the task watchdog (spec 09) can be fed from the AO loop. */
#define NODE_AO_MAX_WAIT_MS 1000

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a node_ao call. */
typedef enum {
  NODE_AO_OK = 0,        /**< Success. */
  NODE_AO_ERR_ARG,       /**< NULL pointer, bad size or bad argument from the
                            caller. */
  NODE_AO_ERR_STATE,     /**< The AO is not started, or already started; or
                            the time event is not initialised. */
  NODE_AO_ERR_FULL,      /**< The AO's queue, or its time event table, is
                            full. A posted event is dropped. */
  NODE_AO_ERR_NO_MEMORY, /**< The port could not create the queue or the
                            thread. */
} node_ao_err_t;

/** @brief Event signal. Each AO defines its own signals, starting at
 *         NODE_AO_SIG_USER. */
typedef uint16_t node_ao_sig_t;

/** @brief Signals reserved by the framework. */
enum {
  NODE_AO_SIG_INIT = 0, /**< Dispatched once, first, in the AO's thread. */
  NODE_AO_SIG_USER = 1, /**< First signal an AO may define. */
};

/** @brief Header of every event. An AO's own event structs start with it. */
typedef struct {
  node_ao_sig_t sig; /**< What happened. */
} node_ao_evt_t;

/** @brief An active object. Embed it as the first member of a component's
 *         AO struct and allocate that struct statically. */
typedef struct node_ao node_ao_t;

/**
 * @brief Handles one event in the AO's thread, to completion.
 *
 * Must not block. @p e is valid only during the call.
 *
 * @param me The AO the event was posted to.
 * @param e  The event; cast it to the AO's event struct by its signal.
 */
typedef void (*node_ao_dispatch_t)(node_ao_t *me, const node_ao_evt_t *e);

/** @brief How an AO's thread and queue are set up. */
typedef struct {
  const char *name;   /**< NUL-terminated thread name, also used in logs;
                         must stay valid while the AO runs. */
  unsigned priority;  /**< FreeRTOS priority of the AO's thread. */
  size_t stack_bytes; /**< Stack size of the AO's thread, in bytes. */
  size_t queue_len;   /**< Events the queue holds; at least 1. */
  size_t evt_size;    /**< Largest event this AO receives, in bytes; from
                         sizeof(node_ao_evt_t) to NODE_AO_MAX_EVT_SIZE. */
} node_ao_config_t;

/** @brief A one-shot or periodic timer owned by one AO, which dispatches it
 *         in its own thread. Allocate statically; set up with
 *         node_ao_time_evt_init(). */
typedef struct node_ao_time_evt {
  node_ao_t *owner;     /**< AO that receives the event; NULL until
                           initialised. */
  node_ao_sig_t sig;    /**< Signal of the dispatched event. */
  bool armed;           /**< True while the time event is counting down. */
  uint32_t deadline_ms; /**< HAL time at which it next fires, in ms; valid
                           while armed. */
  uint32_t period_ms;   /**< Time between firings, in ms; 0 for one-shot. */
} node_ao_time_evt_t;

/** @brief Fields of an active object. Written only by the framework; read
 *         them through the functions below. */
struct node_ao {
  node_ao_dispatch_t dispatch; /**< Event handler; NULL until started. */
  node_ao_config_t cfg;        /**< Copy of the config given at start. */
  bool started;                /**< True once node_ao_start() succeeded. */
  void *queue;                 /**< Port-owned queue handle. */
  void *thread;                /**< Port-owned thread handle. */
  node_ao_time_evt_t *time_evts[NODE_AO_MAX_TIME_EVTS]; /**< Time events
                                                           this AO owns, in
                                                           the order they
                                                           were initialised. */
  size_t n_time_evts; /**< Number of valid entries in @c time_evts. */
};

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Create the AO's queue and thread, and queue its init event.
 *
 * Call once per AO, from task context. The thread dispatches
 * NODE_AO_SIG_INIT before any other event. Time events may be initialised
 * before this call.
 *
 * @param me       AO to start; must not be NULL and must stay valid forever.
 * @param dispatch Event handler; must not be NULL.
 * @param cfg      Thread and queue setup; copied. Must not be NULL.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG for a bad argument or config,
 *         NODE_AO_ERR_STATE if @p me is already started, or
 *         NODE_AO_ERR_NO_MEMORY if the queue or thread cannot be created.
 */
node_ao_err_t node_ao_start(node_ao_t *me, node_ao_dispatch_t dispatch,
                            const node_ao_config_t *cfg);

/**
 * @brief Post an event to an AO from task context. Never blocks.
 *
 * Copies @p size bytes of @p evt and zeroes the rest of the queued slot.
 *
 * @param me   Target AO; must not be NULL.
 * @param evt  Event; starts with a node_ao_evt_t. Must not be NULL.
 * @param size Size of @p evt in bytes; from sizeof(node_ao_evt_t) to the
 *             AO's evt_size.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG, NODE_AO_ERR_STATE if @p me is not
 *         started, or NODE_AO_ERR_FULL if its queue is full and the event
 *         was dropped.
 */
node_ao_err_t node_ao_post(node_ao_t *me, const void *evt, size_t size);

/**
 * @brief Post an event to an AO from ISR context. Never blocks.
 *
 * Same as node_ao_post(), but safe to call only from an ISR. Wakes the AO's
 * thread at the end of the ISR if it has a higher priority.
 *
 * @param me   Target AO; must not be NULL.
 * @param evt  Event; starts with a node_ao_evt_t. Must not be NULL.
 * @param size Size of @p evt in bytes; from sizeof(node_ao_evt_t) to the
 *             AO's evt_size.
 * @return As for node_ao_post().
 */
node_ao_err_t node_ao_post_from_isr(node_ao_t *me, const void *evt,
                                    size_t size);

/**
 * @brief Attach a time event to the AO that will dispatch it, disarmed.
 *
 * Call before node_ao_start(), or from @p owner's dispatch function.
 *
 * @param te    Time event; must not be NULL and must stay valid forever.
 * @param owner AO that dispatches the time event; must not be NULL.
 * @param sig   Signal of the dispatched event; at least NODE_AO_SIG_USER.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG, NODE_AO_ERR_STATE if @p te already
 *         has an owner, or NODE_AO_ERR_FULL if @p owner already has
 *         NODE_AO_MAX_TIME_EVTS time events.
 */
node_ao_err_t node_ao_time_evt_init(node_ao_time_evt_t *te, node_ao_t *owner,
                                    node_ao_sig_t sig);

/**
 * @brief Start, or restart, a time event's countdown.
 *
 * Call only from the owning AO's dispatch function, or before it is started.
 * A periodic time event that falls more than one period behind fires once
 * and is rescheduled one period from then, so it never fires in a burst.
 *
 * @param te        Initialised time event; must not be NULL.
 * @param delay_ms  Time until it first fires, in ms; 0 fires at the next
 *                  turn of the AO loop.
 * @param period_ms Time between later firings, in ms; 0 for one-shot.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG, or NODE_AO_ERR_STATE if @p te is not
 *         initialised.
 */
node_ao_err_t node_ao_time_evt_arm(node_ao_time_evt_t *te, uint32_t delay_ms,
                                   uint32_t period_ms);

/**
 * @brief Stop a time event. Disarming a disarmed time event is allowed.
 *
 * Call only from the owning AO's dispatch function, or before it is started.
 * The event does not fire after this returns.
 *
 * @param te Initialised time event; must not be NULL.
 * @return NODE_AO_OK, NODE_AO_ERR_ARG, or NODE_AO_ERR_STATE if @p te is not
 *         initialised.
 */
node_ao_err_t node_ao_time_evt_disarm(node_ao_time_evt_t *te);

/**
 * @brief Whether a time event is counting down.
 *
 * @param te Time event; NULL counts as disarmed.
 * @return True if @p te is armed.
 */
bool node_ao_time_evt_is_armed(const node_ao_time_evt_t *te);

#ifdef __cplusplus
}
#endif
