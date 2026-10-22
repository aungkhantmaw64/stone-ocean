# 15 — Active object publish-subscribe

**Status:** in-progress
**Depends on:** 14
**Target:** ESP32 · ESP-IDF v5.x / FreeRTOS · `firmware/components/node_ao`

## Goal
Let an active object publish an event to every AO that subscribed to its signal, without knowing who they are. A component that owns data (for example the devices AO of spec 06) publishes changes. Components that need the data subscribe and keep their own copy of the latest values. No state is ever shared between threads.

## Context
- Spec 14 deliberately left publish-subscribe out. This spec adds it to `node_ao` without changing anything spec 14 already provides.
- First users:
  - The devices AO (spec 06) publishes readings, motion, on/off changes and faults.
  - The mesh model mapping (spec 11) subscribes to them, caches the latest values and answers mesh Get requests from that cache.
- Pattern rules for the whole firmware:
  - **Data that changes is published.** Subscribers keep the latest value in their own state instead of reading another AO's state.
  - **A command has one owner, so it is posted directly** with `node_ao_post` (for example spec 06's on/off command to the devices AO). It does not go through publish-subscribe.
  - **Request and reply:** if a component ever needs an on-demand value, the request event carries the requester's `node_ao_t *` and the owner posts the reply directly to it. No current spec needs this, so it is not built here.
- Published events are copied by value into each subscriber's queue, the same way `node_ao_post` copies them. There is no broker thread, no event pool and no reference counting.

## Hardware details
N/A. Uses only the existing `node_ao` port.

## Scope
- In scope:
  - A signal range reserved for published signals, separate from the private signals each AO defines for itself.
  - A project-wide header that gives each publishing component its own block of published signals, so they never collide.
  - `node_ao_pubsub_subscribe`: subscribe an AO to one published signal.
  - `node_ao_pubsub_publish`: deliver an event to every subscriber of its signal.
  - A static subscription table, with no unsubscribe.
  - Clearing the subscription table from `node_ao_port_mock_reset()`, so host tests stay independent.
- Out of scope: unsubscribing, publishing from an ISR, wildcard or range subscriptions, priority-ordered delivery, request/reply helpers, changes to the HAL self-test.

## Interface
```c
/* node_ao.h: additions */
enum { NODE_AO_SIG_PUB_FIRST = 256 };   /* published signals are >= this; private ones are
                                           NODE_AO_SIG_USER .. NODE_AO_SIG_PUB_FIRST - 1 */
#define NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS 32   /* (AO, signal) pairs in the whole firmware */

/* node_ao_pub_sigs.h: project-wide registry of published signal blocks */
#define NODE_AO_PUB_SIGS_PER_COMPONENT 16
enum {
    NODE_AO_PUB_SIGS_NODE_DEV = NODE_AO_SIG_PUB_FIRST,   /* spec 06; first block */
    /* later publishers add their block below, NODE_AO_PUB_SIGS_PER_COMPONENT apart */
};

/* node_ao_pubsub.c */
node_ao_err_t node_ao_pubsub_subscribe(node_ao_t *me, node_ao_sig_t sig);
node_ao_err_t node_ao_pubsub_publish(const void *evt, size_t size);   /* task context; never blocks */
```
A publishing component defines its signals from its block, for example `NODE_DEV_SIG_READING = NODE_AO_PUB_SIGS_NODE_DEV`.

## Behavior & constraints
- **Signals:**
  - `node_ao_pubsub_subscribe` and `node_ao_pubsub_publish` reject a signal below `NODE_AO_SIG_PUB_FIRST` with `NODE_AO_ERR_ARG`.
  - Private signals (time events, commands posted directly) stay below it, so an AO's private signals can never clash with a published signal it subscribes to.
- **Subscribing:**
  - Allowed from task context, before or after the subscribing AO is started. Usually a component subscribes in its start function or in its init dispatch.
  - Subscribing the same AO to the same signal twice is a no-op that returns `NODE_AO_OK`.
  - A full table returns `NODE_AO_ERR_FULL`.
  - The table is append-only. A subscription made while other AOs are publishing is safe. It takes effect when `node_ao_pubsub_subscribe` returns, and an event published concurrently may or may not reach it.
- **Publishing:**
  - Copies `size` bytes (at least `sizeof(node_ao_evt_t)`, at most `NODE_AO_MAX_EVT_SIZE`) into the queue of every started subscriber of the event's signal, in the order they subscribed, using the same rules as `node_ao_post`.
  - Never blocks. Callable from any task, including an AO's own dispatch. Not callable from an ISR.
  - Subscribers that are not started yet are skipped.
  - Every subscriber is tried even when an earlier one fails. The return value is `NODE_AO_OK` if every started subscriber queued the event (or there were none). Otherwise it is the error of the first subscriber that did not queue it: `NODE_AO_ERR_FULL` for a full queue, `NODE_AO_ERR_ARG` for an event larger than that subscriber's `evt_size`.
  - Each subscriber receives a publisher's events in the order they were published. An AO that subscribes to a signal it publishes receives its own events too.
- All memory is static. Nothing is allocated.
- `node_ao_pub_sigs.h` and the additions to `node_ao.h` include no FreeRTOS or ESP-IDF headers.

## Verification
- **Simulation/host:** Unity tests with the host port and `node_ao_port_mock_run_until_idle()` cover:
  - a published event reaches every subscriber of its signal, and no other AO
  - subscribers receive published events in publish order
  - a private signal is rejected for publishing and for subscribing
  - subscribing twice delivers the event once
  - a full subscription table is rejected
  - a full subscriber queue is reported and does not stop delivery to the other subscribers
  - an event larger than a subscriber's event size is reported, and the other subscribers still receive it
  - subscribers that are not started yet are skipped
  - publishing with no subscribers succeeds
  - an AO can publish from its own dispatch, including to itself
  - the mock reset forgets every subscription
- **On hardware:** nothing beyond spec 14. Publish-subscribe is exercised on the board through spec 06's devices AO.

## Acceptance criteria
- [ ] Published events reach exactly the subscribers of their signal, in publish order (host tests).
- [ ] Bad signals, duplicate subscriptions and a full subscription table are handled as specified (host tests).
- [ ] Publishing never blocks, reports a full or too-small subscriber queue, and still delivers to the other subscribers (host tests).
- [ ] The new headers compile in the ESP-IDF and host builds with no FreeRTOS or ESP-IDF includes.

## Open questions
- None.
