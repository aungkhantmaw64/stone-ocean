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

/** @brief Events the test AOs' queues hold, unless a test asks for fewer.
 *         The init event takes one slot until it is dispatched. */
#define QUEUE_LEN 4

/** @brief Most events the recorder keeps. */
#define MAX_RECORDED 16

/** @brief Value carried by test events, to check it arrives unchanged. */
#define TEST_VALUE 0xC0FFEE42u

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Signals used by the tests. */
enum {
  SIG_PRIVATE = NODE_AO_SIG_USER,    /**< A private signal; not publishable. */
  SIG_TRIGGER,                       /**< Posted to ask an AO to publish. */
  SIG_PUB_A = NODE_AO_SIG_PUB_FIRST, /**< First published test signal. */
  SIG_PUB_B,                         /**< Second published test signal. */
};

/** @brief A short event: header plus one value. */
typedef struct {
  node_ao_evt_t super; /**< Header. */
  uint32_t value;      /**< Payload. */
} value_evt_t;

/** @brief An event one byte larger than the framework allows. */
typedef struct {
  node_ao_evt_t super; /**< Header. */
  uint8_t bytes[NODE_AO_MAX_EVT_SIZE + 1 - sizeof(node_ao_evt_t)]; /**< Payload.
                                                                    */
} oversized_evt_t;

/** @brief What the recorder saw for one dispatched event other than init. */
typedef struct {
  node_ao_t *ao;     /**< AO that received it. */
  node_ao_sig_t sig; /**< Its signal. */
  uint32_t value;    /**< Its value, if the receiving AO takes value events;
                        otherwise 0. */
} recorded_evt_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief First test AO. */
static node_ao_t g_ao = {0};

/** @brief Second test AO. */
static node_ao_t g_other_ao = {0};

/** @brief Third test AO. */
static node_ao_t g_third_ao = {0};

/** @brief Events other than init dispatched since setUp(), in dispatch
 *         order. */
static recorded_evt_t g_recorded[MAX_RECORDED] = {0};

/** @brief Number of valid entries in g_recorded. */
static size_t g_recorded_count = 0;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Dispatch function that records every event except init.
 *
 * @param me AO that received the event.
 * @param e  The event.
 */
static void record_event(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief record_event() that also publishes a SIG_PUB_A value event when it
 *        receives SIG_TRIGGER.
 *
 * @param me AO that received the event.
 * @param e  The event.
 */
static void record_and_publish_on_trigger(node_ao_t *me,
                                          const node_ao_evt_t *e);

/**
 * @brief A valid config for an AO.
 *
 * @param evt_size  Event size of the AO.
 * @param queue_len Events its queue holds.
 * @return The config.
 */
static node_ao_config_t test_config(size_t evt_size, size_t queue_len);

/**
 * @brief Start @p ao with record_event() and a queue of QUEUE_LEN value
 *        events, and fail the test if that fails.
 *
 * @param ao AO to start.
 */
static void start_recorder(node_ao_t *ao);

/**
 * @brief Subscribe @p ao to @p sig and fail the test if that fails.
 *
 * @param ao  AO to subscribe.
 * @param sig Published signal.
 */
static void subscribe(node_ao_t *ao, node_ao_sig_t sig);

/**
 * @brief A value event with signal @p sig and @p value.
 *
 * @param sig   Signal of the event.
 * @param value Payload.
 * @return The event.
 */
static value_evt_t value_event(node_ao_sig_t sig, uint32_t value);

/**
 * @brief Number of recorded events that @p ao received with signal @p sig.
 *
 * @param ao  Receiving AO.
 * @param sig Signal to count.
 * @return The count.
 */
static size_t received_count(const node_ao_t *ao, node_ao_sig_t sig);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  g_ao = (node_ao_t){0};
  g_other_ao = (node_ao_t){0};
  g_third_ao = (node_ao_t){0};
  (void)memset(g_recorded, 0, sizeof(g_recorded));
  g_recorded_count = 0;
}

void tearDown(void) {}

/* ---- Delivering published events ---- */

/* AC: specs/15-ao-publish-subscribe.md #1 */
void test_published_event_reaches_every_active_object_subscribed_to_its_signal(
    void) {
  start_recorder(&g_ao);
  start_recorder(&g_other_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_other_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, err);
  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_ao, SIG_PUB_A));
  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_other_ao, SIG_PUB_A));
}

/* AC: specs/15-ao-publish-subscribe.md #1 */
void test_published_event_does_not_reach_an_active_object_subscribed_to_a_different_signal(
    void) {
  start_recorder(&g_ao);
  start_recorder(&g_other_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_other_ao, SIG_PUB_B);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, g_recorded_count);
  TEST_ASSERT_EQUAL_PTR(&g_ao, g_recorded[0].ao);
}

/* AC: specs/15-ao-publish-subscribe.md #1 */
void test_subscriber_receives_published_events_in_the_order_they_were_published(
    void) {
  start_recorder(&g_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_ao, SIG_PUB_B);
  value_evt_t first = value_event(SIG_PUB_B, 1);
  value_evt_t second = value_event(SIG_PUB_A, 2);
  value_evt_t third = value_event(SIG_PUB_B, 3);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_pubsub_publish(&first, sizeof(first)));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_pubsub_publish(&second, sizeof(second)));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_pubsub_publish(&third, sizeof(third)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(3, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT32(1, g_recorded[0].value);
  TEST_ASSERT_EQUAL_UINT32(2, g_recorded[1].value);
  TEST_ASSERT_EQUAL_UINT32(3, g_recorded[2].value);
}

/* AC: specs/15-ao-publish-subscribe.md #1 */
void test_published_event_contents_reach_each_subscriber_unchanged(void) {
  start_recorder(&g_ao);
  start_recorder(&g_other_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_other_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(2, g_recorded_count);
  for (size_t i = 0; i < g_recorded_count; i++) {
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(SIG_PUB_A, g_recorded[i].sig,
                                     "a subscriber got the wrong signal");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(TEST_VALUE, g_recorded[i].value,
                                    "a subscriber got the wrong value");
  }
}

/* AC: specs/15-ao-publish-subscribe.md #1 */
void test_active_object_subscribed_before_it_was_started_receives_events_published_after_it_starts(
    void) {
  subscribe(&g_ao, SIG_PUB_A);
  start_recorder(&g_ao);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_ao, SIG_PUB_A));
}

/* AC: specs/15-ao-publish-subscribe.md #1 */
void test_active_object_can_publish_while_handling_an_event_and_receives_its_own_published_event(
    void) {
  node_ao_config_t cfg = test_config(sizeof(value_evt_t), QUEUE_LEN);
  TEST_ASSERT_EQUAL_INT(
      NODE_AO_OK, node_ao_start(&g_ao, record_and_publish_on_trigger, &cfg));
  start_recorder(&g_other_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_other_ao, SIG_PUB_A);
  const node_ao_evt_t trigger = {.sig = SIG_TRIGGER};

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_post(&g_ao, &trigger, sizeof(trigger)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_ao, SIG_PUB_A));
  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_other_ao, SIG_PUB_A));
}

/* AC: specs/15-ao-publish-subscribe.md #3 */
void test_publishing_with_no_subscribers_succeeds(void) {
  start_recorder(&g_ao);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, err);
  TEST_ASSERT_EQUAL_size_t(0, g_recorded_count);
}

/* ---- Rejecting bad signals and subscriptions ---- */

/* AC: specs/15-ao-publish-subscribe.md #2 */
void test_subscribing_to_a_private_signal_is_rejected(void) {
  node_ao_err_t err = node_ao_pubsub_subscribe(&g_ao, SIG_PRIVATE);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
}

/* AC: specs/15-ao-publish-subscribe.md #2 */
void test_publishing_an_event_with_a_private_signal_is_rejected_and_reaches_no_one(
    void) {
  start_recorder(&g_ao);
  subscribe(&g_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PRIVATE, TEST_VALUE);

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, g_recorded_count);
}

/* AC: specs/15-ao-publish-subscribe.md #2 */
void test_event_smaller_than_the_event_header_is_rejected_for_publishing(void) {
  start_recorder(&g_ao);
  subscribe(&g_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(node_ao_evt_t) - 1);
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, g_recorded_count);
}

/* AC: specs/15-ao-publish-subscribe.md #2 */
void test_event_larger_than_the_largest_allowed_event_is_rejected_for_publishing(
    void) {
  start_recorder(&g_ao);
  subscribe(&g_ao, SIG_PUB_A);
  oversized_evt_t evt = {.super = {.sig = SIG_PUB_A}};

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, g_recorded_count);
}

/* AC: specs/15-ao-publish-subscribe.md #2 */
void test_subscribing_twice_to_the_same_signal_delivers_each_published_event_once(
    void) {
  start_recorder(&g_ao);
  subscribe(&g_ao, SIG_PUB_A);
  node_ao_err_t err = node_ao_pubsub_subscribe(&g_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, err);
  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_ao, SIG_PUB_A));
}

/* AC: specs/15-ao-publish-subscribe.md #2 */
void test_subscription_is_rejected_when_the_subscription_table_is_full(void) {
  for (size_t i = 0; i < NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS; i++) {
    subscribe(&g_ao, (node_ao_sig_t)(NODE_AO_SIG_PUB_FIRST + i));
  }
  const node_ao_sig_t one_signal_too_many =
      (node_ao_sig_t)(NODE_AO_SIG_PUB_FIRST + NODE_AO_PUBSUB_MAX_SUBSCRIPTIONS);

  node_ao_err_t err = node_ao_pubsub_subscribe(&g_ao, one_signal_too_many);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_FULL, err);
}

/* ---- Failures while publishing ---- */

/* AC: specs/15-ao-publish-subscribe.md #3 */
void test_full_subscriber_queue_is_reported_and_the_other_subscribers_still_receive_the_event(
    void) {
  node_ao_config_t one_slot = test_config(sizeof(value_evt_t), 1);
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_start(&g_ao, record_event, &one_slot));
  start_recorder(&g_other_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_other_ao, SIG_PUB_A);
  /* Dispatch the init events, so g_ao's single slot is free. */
  node_ao_port_mock_run_until_idle();
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_FULL, err);
  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_ao, SIG_PUB_A));
  TEST_ASSERT_EQUAL_size_t(2, received_count(&g_other_ao, SIG_PUB_A));
}

/* AC: specs/15-ao-publish-subscribe.md #3 */
void test_publishing_an_event_too_large_for_one_subscriber_is_reported_and_the_others_still_receive_it(
    void) {
  node_ao_config_t header_only = test_config(sizeof(node_ao_evt_t), QUEUE_LEN);
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_start(&g_ao, record_event, &header_only));
  start_recorder(&g_other_ao);
  subscribe(&g_ao, SIG_PUB_A);
  subscribe(&g_other_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
  TEST_ASSERT_EQUAL_size_t(0, received_count(&g_ao, SIG_PUB_A));
  TEST_ASSERT_EQUAL_size_t(1, received_count(&g_other_ao, SIG_PUB_A));
}

/* AC: specs/15-ao-publish-subscribe.md #3 */
void test_subscriber_that_is_not_started_is_skipped_without_reporting_an_error(
    void) {
  subscribe(&g_ao, SIG_PUB_A);
  start_recorder(&g_other_ao);
  subscribe(&g_other_ao, SIG_PUB_A);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  node_ao_err_t err = node_ao_pubsub_publish(&evt, sizeof(evt));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, err);
  TEST_ASSERT_EQUAL_size_t(1, g_recorded_count);
  TEST_ASSERT_EQUAL_PTR(&g_other_ao, g_recorded[0].ao);
}

/* ---- Host test port ---- */

/* Spec 15 Scope: the mock reset clears subscriptions so tests stay
 * independent. */
void test_resetting_the_host_test_port_forgets_every_subscription(void) {
  start_recorder(&g_ao);
  subscribe(&g_ao, SIG_PUB_A);
  node_ao_port_mock_reset();
  start_recorder(&g_ao);
  value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);

  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_publish(&evt, sizeof(evt)));
  node_ao_port_mock_run_until_idle();

  TEST_ASSERT_EQUAL_size_t(0, g_recorded_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_published_event_reaches_every_active_object_subscribed_to_its_signal);
  RUN_TEST(
      test_published_event_does_not_reach_an_active_object_subscribed_to_a_different_signal);
  RUN_TEST(
      test_subscriber_receives_published_events_in_the_order_they_were_published);
  RUN_TEST(test_published_event_contents_reach_each_subscriber_unchanged);
  RUN_TEST(
      test_active_object_subscribed_before_it_was_started_receives_events_published_after_it_starts);
  RUN_TEST(
      test_active_object_can_publish_while_handling_an_event_and_receives_its_own_published_event);
  RUN_TEST(test_publishing_with_no_subscribers_succeeds);
  RUN_TEST(test_subscribing_to_a_private_signal_is_rejected);
  RUN_TEST(
      test_publishing_an_event_with_a_private_signal_is_rejected_and_reaches_no_one);
  RUN_TEST(test_event_smaller_than_the_event_header_is_rejected_for_publishing);
  RUN_TEST(
      test_event_larger_than_the_largest_allowed_event_is_rejected_for_publishing);
  RUN_TEST(
      test_subscribing_twice_to_the_same_signal_delivers_each_published_event_once);
  RUN_TEST(test_subscription_is_rejected_when_the_subscription_table_is_full);
  RUN_TEST(
      test_full_subscriber_queue_is_reported_and_the_other_subscribers_still_receive_the_event);
  RUN_TEST(
      test_publishing_an_event_too_large_for_one_subscriber_is_reported_and_the_others_still_receive_it);
  RUN_TEST(
      test_subscriber_that_is_not_started_is_skipped_without_reporting_an_error);
  RUN_TEST(test_resetting_the_host_test_port_forgets_every_subscription);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void record_event(node_ao_t *me, const node_ao_evt_t *e) {
  if (e->sig == NODE_AO_SIG_INIT) {
    return;
  }
  if (g_recorded_count >= MAX_RECORDED) {
    TEST_FAIL_MESSAGE("more events than the recorder holds");
  }
  recorded_evt_t *rec = &g_recorded[g_recorded_count++];
  rec->ao = me;
  rec->sig = e->sig;
  if (me->cfg.evt_size >= sizeof(value_evt_t)) {
    rec->value = ((const value_evt_t *)e)->value;
  }
}

static void record_and_publish_on_trigger(node_ao_t *me,
                                          const node_ao_evt_t *e) {
  record_event(me, e);
  if (e->sig == SIG_TRIGGER) {
    value_evt_t evt = value_event(SIG_PUB_A, TEST_VALUE);
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_pubsub_publish(&evt, sizeof(evt)));
  }
}

static node_ao_config_t test_config(size_t evt_size, size_t queue_len) {
  return (node_ao_config_t){
      .name = "test_ao",
      .priority = 1,
      .stack_bytes = 2048,
      .queue_len = queue_len,
      .evt_size = evt_size,
  };
}

static void start_recorder(node_ao_t *ao) {
  node_ao_config_t cfg = test_config(sizeof(value_evt_t), QUEUE_LEN);
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_start(ao, record_event, &cfg));
}

static void subscribe(node_ao_t *ao, node_ao_sig_t sig) {
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_pubsub_subscribe(ao, sig));
}

static value_evt_t value_event(node_ao_sig_t sig, uint32_t value) {
  return (value_evt_t){.super = {.sig = sig}, .value = value};
}

static size_t received_count(const node_ao_t *ao, node_ao_sig_t sig) {
  size_t count = 0;
  for (size_t i = 0; i < g_recorded_count; i++) {
    if (g_recorded[i].ao == ao && g_recorded[i].sig == sig) {
      count++;
    }
  }
  return count;
}
