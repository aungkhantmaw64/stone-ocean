/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_ao.h"
#include "node_ao_port_mock.h"
#include "node_hal_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Events the test AOs' queues hold. The init event takes one slot
 *         until it is dispatched. */
#define QUEUE_LEN 4

/** @brief Most events the recorder keeps. */
#define MAX_RECORDED 16

/** @brief Value carried by test events, to check it arrives unchanged. */
#define TEST_VALUE 0xC0FFEE42u

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Signals of the test AOs. */
enum {
  SIG_FIRST = NODE_AO_SIG_USER, /**< First posted test event. */
  SIG_SECOND,                   /**< Second posted test event. */
  SIG_PING,                     /**< Bounced between two AOs forever. */
};

/** @brief A short event: header plus one value. */
typedef struct {
  node_ao_evt_t super; /**< Header. */
  uint32_t value;      /**< Payload. */
} value_evt_t;

/** @brief The largest event the framework allows. */
typedef struct {
  node_ao_evt_t super;                                         /**< Header. */
  uint8_t bytes[NODE_AO_MAX_EVT_SIZE - sizeof(node_ao_evt_t)]; /**< Payload. */
} largest_evt_t;

/** @brief What the recorder saw for one dispatched event. */
typedef struct {
  node_ao_t *ao;                       /**< AO that received it. */
  node_ao_sig_t sig;                   /**< Its signal. */
  uint8_t bytes[NODE_AO_MAX_EVT_SIZE]; /**< Its queued slot, evt_size bytes
                                          of the receiving AO. */
} recorded_evt_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief The AO under test. */
static node_ao_t g_ao = {0};

/** @brief A second AO, for tests that need two. */
static node_ao_t g_other_ao = {0};

/** @brief Events dispatched since setUp(), in dispatch order. */
static recorded_evt_t g_recorded[MAX_RECORDED] = {0};

/** @brief Number of valid entries in g_recorded. */
static size_t g_recorded_count = 0;

/** @brief Event size of the AOs started with start_recorder(). */
static size_t g_evt_size = 0;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Dispatch function that records every event it receives.
 *
 * @param me AO that received the event.
 * @param e  The event.
 */
static void record_event(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief record_event() that also posts SIG_FIRST to itself on its init
 *        event.
 *
 * @param me AO that received the event.
 * @param e  The event.
 */
static void record_and_post_to_self_on_init(node_ao_t *me,
                                            const node_ao_evt_t *e);

/**
 * @brief Dispatch function that answers every SIG_PING with a SIG_PING to
 *        the other test AO.
 *
 * @param me AO that received the event.
 * @param e  The event.
 */
static void bounce_ping(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief A valid config with QUEUE_LEN slots of @p evt_size bytes.
 *
 * @param evt_size Event size of the AO.
 * @return The config.
 */
static node_ao_config_t config_with_event_size(size_t evt_size);

/**
 * @brief Start @p ao with record_event() and fail the test if that fails.
 *
 * @param ao       AO to start.
 * @param evt_size Event size of the AO.
 */
static void start_recorder(node_ao_t *ao, size_t evt_size);

/**
 * @brief A value event with signal @p sig and TEST_VALUE.
 *
 * @param sig Signal of the event.
 * @return The event.
 */
static value_evt_t value_event(node_ao_sig_t sig);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  g_ao = (node_ao_t){0};
  g_other_ao = (node_ao_t){0};
  (void)memset(g_recorded, 0, sizeof(g_recorded));
  g_recorded_count = 0;
  g_evt_size = 0;
}

void tearDown(void) {}

/* ---- Starting active objects ---- */

/* AC: specs/14-active-objects.md #1 */
void test_active_object_handles_its_init_event_before_any_posted_event(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_post(&g_ao, &first, sizeof(first)));

  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(2, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(NODE_AO_SIG_INIT, g_recorded[0].sig);
  TEST_ASSERT_EQUAL_UINT16(SIG_FIRST, g_recorded[1].sig);
}

/* AC: specs/14-active-objects.md #1 */
void test_active_object_can_post_to_itself_while_handling_its_init_event(void) {
  node_ao_config_t cfg = config_with_event_size(sizeof(value_evt_t));
  g_evt_size = cfg.evt_size;
  TEST_ASSERT_EQUAL_INT(
      NODE_AO_OK, node_ao_start(&g_ao, record_and_post_to_self_on_init, &cfg));

  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(2, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(SIG_FIRST, g_recorded[1].sig);
}

/* AC: specs/14-active-objects.md #2 */
void test_starting_an_active_object_twice_is_rejected(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  node_ao_config_t cfg = config_with_event_size(sizeof(value_evt_t));

  node_ao_err_t err = node_ao_start(&g_ao, record_event, &cfg);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_STATE, err);
}

/* AC: specs/14-active-objects.md #2 */
void test_active_object_is_not_started_with_an_invalid_config(void) {
  node_ao_config_t valid = config_with_event_size(sizeof(value_evt_t));
  node_ao_config_t no_name = config_with_event_size(sizeof(value_evt_t));
  no_name.name = NULL;
  node_ao_config_t empty_queue = config_with_event_size(sizeof(value_evt_t));
  empty_queue.queue_len = 0;
  node_ao_config_t no_stack = config_with_event_size(sizeof(value_evt_t));
  no_stack.stack_bytes = 0;
  node_ao_config_t smaller_than_header =
      config_with_event_size(sizeof(node_ao_evt_t) - 1);
  node_ao_config_t larger_than_maximum =
      config_with_event_size(NODE_AO_MAX_EVT_SIZE + 1);
  const struct {
    const char *name;
    node_ao_dispatch_t dispatch;
    const node_ao_config_t *cfg;
  } cases[] = {
      {"no event handler", NULL, &valid},
      {"no config", record_event, NULL},
      {"no thread name", record_event, &no_name},
      {"empty queue", record_event, &empty_queue},
      {"no stack", record_event, &no_stack},
      {"event size smaller than the header", record_event,
       &smaller_than_header},
      {"event size larger than the maximum", record_event,
       &larger_than_maximum},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    node_ao_err_t err = node_ao_start(&g_ao, cases[i].dispatch, cases[i].cfg);

    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_AO_ERR_ARG, err, cases[i].name);
    value_evt_t first = value_event(SIG_FIRST);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_AO_ERR_STATE,
                                  node_ao_post(&g_ao, &first, sizeof(first)),
                                  cases[i].name);
  }
}

/* AC: specs/14-active-objects.md #2 */
void test_active_object_can_be_started_after_a_rejected_start(void) {
  /* The host port, like FreeRTOS, refuses a queue it cannot hold. */
  node_ao_config_t too_long = config_with_event_size(sizeof(value_evt_t));
  too_long.queue_len = NODE_AO_PORT_MOCK_MAX_QUEUE_LEN + 1;
  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG,
                        node_ao_start(&g_ao, record_event, &too_long));
  node_ao_config_t cfg = config_with_event_size(sizeof(value_evt_t));
  g_evt_size = cfg.evt_size;

  node_ao_err_t err = node_ao_start(&g_ao, record_event, &cfg);
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, err);
  TEST_ASSERT_EQUAL_size_t(1, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(NODE_AO_SIG_INIT, g_recorded[0].sig);
}

/* ---- Delivering events ---- */

/* AC: specs/14-active-objects.md #1 */
void test_active_object_receives_events_in_the_order_they_were_posted(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);
  value_evt_t second = value_event(SIG_SECOND);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_post(&g_ao, &first, sizeof(first)));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_post(&g_ao, &second, sizeof(second)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(3, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(SIG_FIRST, g_recorded[1].sig);
  TEST_ASSERT_EQUAL_UINT16(SIG_SECOND, g_recorded[2].sig);
}

/* AC: specs/14-active-objects.md #1 */
void test_event_contents_reach_the_active_object_unchanged(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_post(&g_ao, &first, sizeof(first)));
  node_ao_port_mock_run_until_idle();

  value_evt_t received = {0};
  (void)memcpy(&received, g_recorded[1].bytes, sizeof(received));
  TEST_ASSERT_EQUAL_UINT16(SIG_FIRST, received.super.sig);
  TEST_ASSERT_EQUAL_HEX32(TEST_VALUE, received.value);
}

/* AC: specs/14-active-objects.md #2 */
void test_unused_bytes_of_a_short_event_arrive_as_zeros(void) {
  start_recorder(&g_ao, sizeof(largest_evt_t));
  largest_evt_t filler = {.super = {.sig = SIG_FIRST}};
  (void)memset(filler.bytes, 0xFF, sizeof(filler.bytes));
  value_evt_t short_evt = value_event(SIG_SECOND);
  static const uint8_t zeros[sizeof(largest_evt_t) - sizeof(value_evt_t)] = {0};

  /* Every queue slot first holds a full-size event of 0xFF bytes, so stale
   * bytes would show if a slot were reused without zeroing. The init event
   * holds one slot until it is dispatched, hence the second round. */
  for (size_t i = 0; i < QUEUE_LEN - 1; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_post(&g_ao, &filler, sizeof(filler)));
  }
  node_ao_port_mock_run_until_idle();
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_post(&g_ao, &filler, sizeof(filler)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_post(&g_ao, &short_evt, sizeof(short_evt)));
  node_ao_port_mock_run_until_idle();

  const recorded_evt_t *received = &g_recorded[g_recorded_count - 1];
  TEST_ASSERT_EQUAL_UINT16(SIG_SECOND, received->sig);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, &received->bytes[sizeof(value_evt_t)],
                               sizeof(zeros));
}

/* AC: specs/14-active-objects.md #2 */
void test_event_posted_from_an_interrupt_is_delivered(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_post_from_isr(&g_ao, &first, sizeof(first)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(2, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(SIG_FIRST, g_recorded[1].sig);
}

/* ---- Rejecting posts ---- */

/* AC: specs/14-active-objects.md #2 */
void test_event_posted_to_an_active_object_that_is_not_started_is_rejected(
    void) {
  value_evt_t first = value_event(SIG_FIRST);

  node_ao_err_t err = node_ao_post(&g_ao, &first, sizeof(first));

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_STATE, err);
}

/* AC: specs/14-active-objects.md #2 */
void test_event_smaller_than_the_event_header_is_rejected(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);

  node_ao_err_t err = node_ao_post(&g_ao, &first, sizeof(node_ao_evt_t) - 1);
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(1, g_recorded_count);
}

/* AC: specs/14-active-objects.md #2 */
void test_event_larger_than_the_active_objects_event_size_is_rejected(void) {
  start_recorder(&g_ao, sizeof(node_ao_evt_t));
  value_evt_t first = value_event(SIG_FIRST);

  node_ao_err_t err = node_ao_post(&g_ao, &first, sizeof(first));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(1, g_recorded_count);
}

/* AC: specs/14-active-objects.md #2 */
void test_event_posted_to_a_full_queue_is_rejected_and_the_queued_events_are_kept(
    void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);
  value_evt_t second = value_event(SIG_SECOND);
  /* The init event holds one slot, so QUEUE_LEN - 1 posts fill the queue. */
  for (size_t i = 0; i < QUEUE_LEN - 1; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_post(&g_ao, &first, sizeof(first)));
  }

  node_ao_err_t err = node_ao_post(&g_ao, &second, sizeof(second));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_FULL, err);
  TEST_ASSERT_EQUAL_size_t(QUEUE_LEN, g_recorded_count);
  for (size_t i = 1; i < QUEUE_LEN; i++) {
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(SIG_FIRST, g_recorded[i].sig,
                                     "a queued event was lost");
  }
}

/* AC: specs/14-active-objects.md #2 */
void test_event_posted_from_an_interrupt_to_a_full_queue_is_rejected(void) {
  start_recorder(&g_ao, sizeof(value_evt_t));
  value_evt_t first = value_event(SIG_FIRST);
  for (size_t i = 0; i < QUEUE_LEN - 1; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_post_from_isr(&g_ao, &first, sizeof(first)));
  }

  node_ao_err_t err = node_ao_post_from_isr(&g_ao, &first, sizeof(first));

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_FULL, err);
}

/* ---- Host test runner ---- */

/* Spec 14 Verification: an endless ping-pong must fail the test, not hang. */
void test_endless_event_ping_pong_between_two_active_objects_is_stopped(void) {
  node_ao_config_t cfg = config_with_event_size(sizeof(node_ao_evt_t));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_start(&g_ao, bounce_ping, &cfg));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_start(&g_other_ao, bounce_ping, &cfg));
  const node_ao_evt_t ping = {.sig = SIG_PING};
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_post(&g_ao, &ping, sizeof(ping)));

  int dispatched = node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY, dispatched);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_active_object_handles_its_init_event_before_any_posted_event);
  RUN_TEST(test_active_object_can_post_to_itself_while_handling_its_init_event);
  RUN_TEST(test_starting_an_active_object_twice_is_rejected);
  RUN_TEST(test_active_object_is_not_started_with_an_invalid_config);
  RUN_TEST(test_active_object_can_be_started_after_a_rejected_start);
  RUN_TEST(test_active_object_receives_events_in_the_order_they_were_posted);
  RUN_TEST(test_event_contents_reach_the_active_object_unchanged);
  RUN_TEST(test_unused_bytes_of_a_short_event_arrive_as_zeros);
  RUN_TEST(test_event_posted_from_an_interrupt_is_delivered);
  RUN_TEST(
      test_event_posted_to_an_active_object_that_is_not_started_is_rejected);
  RUN_TEST(test_event_smaller_than_the_event_header_is_rejected);
  RUN_TEST(test_event_larger_than_the_active_objects_event_size_is_rejected);
  RUN_TEST(
      test_event_posted_to_a_full_queue_is_rejected_and_the_queued_events_are_kept);
  RUN_TEST(test_event_posted_from_an_interrupt_to_a_full_queue_is_rejected);
  RUN_TEST(test_endless_event_ping_pong_between_two_active_objects_is_stopped);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void record_event(node_ao_t *me, const node_ao_evt_t *e) {
  if (g_recorded_count >= MAX_RECORDED) {
    TEST_FAIL_MESSAGE("more events than the recorder holds");
  }
  recorded_evt_t *rec = &g_recorded[g_recorded_count++];
  rec->ao = me;
  rec->sig = e->sig;
  /* The init event is only a header; posted events fill the whole slot. */
  size_t len =
      (e->sig == NODE_AO_SIG_INIT) ? sizeof(node_ao_evt_t) : g_evt_size;
  (void)memcpy(rec->bytes, e, len);
}

static void record_and_post_to_self_on_init(node_ao_t *me,
                                            const node_ao_evt_t *e) {
  record_event(me, e);
  if (e->sig == NODE_AO_SIG_INIT) {
    value_evt_t first = value_event(SIG_FIRST);
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_post(me, &first, sizeof(first)));
  }
}

static void bounce_ping(node_ao_t *me, const node_ao_evt_t *e) {
  if (e->sig == SIG_PING) {
    node_ao_t *other = (me == &g_ao) ? &g_other_ao : &g_ao;
    (void)node_ao_post(other, e, sizeof(*e));
  }
}

static node_ao_config_t config_with_event_size(size_t evt_size) {
  return (node_ao_config_t){
      .name = "test_ao",
      .priority = 1,
      .stack_bytes = 2048,
      .queue_len = QUEUE_LEN,
      .evt_size = evt_size,
  };
}

static void start_recorder(node_ao_t *ao, size_t evt_size) {
  node_ao_config_t cfg = config_with_event_size(evt_size);
  g_evt_size = evt_size;
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_start(ao, record_event, &cfg));
}

static value_evt_t value_event(node_ao_sig_t sig) {
  return (value_evt_t){.super = {.sig = sig}, .value = TEST_VALUE};
}
