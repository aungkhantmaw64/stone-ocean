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

/** @brief Events the test AO's queue holds. */
#define QUEUE_LEN 4

/** @brief Most events the recorder keeps. */
#define MAX_RECORDED 16

/** @brief Delay and period used by most tests, in ms. */
#define PERIOD_MS 100u

/** @brief Longest delay or period a time event accepts: INT32_MAX ms,
 *         about 24.8 days. */
#define MAX_DELAY_MS ((uint32_t)INT32_MAX)

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Signals of the test AO. */
enum {
  SIG_TICK = NODE_AO_SIG_USER, /**< Fired by g_tick. */
  SIG_TOCK,                    /**< Fired by g_tock. */
  SIG_EVENT,                   /**< A posted event. */
  SIG_REARM,                   /**< Asks the AO to arm g_tick again. */
  SIG_DISARM,                  /**< Asks the AO to disarm g_tick. */
};

/** @brief Asks the test AO to arm g_tick with these times. */
typedef struct {
  node_ao_evt_t super; /**< Signal SIG_REARM. */
  uint32_t delay_ms;   /**< Delay to arm g_tick with, in ms. */
  uint32_t period_ms;  /**< Period to arm g_tick with, in ms. */
} rearm_evt_t;

/** @brief One dispatched event, as the recorder saw it. */
typedef struct {
  node_ao_sig_t sig; /**< Its signal. */
  uint32_t at_ms;    /**< Mock HAL time when it was dispatched, in ms. */
} recorded_evt_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief The AO under test. */
static node_ao_t g_ao = {0};

/** @brief A second AO, for the ownership test. */
static node_ao_t g_other_ao = {0};

/** @brief Time event under test; fires SIG_TICK. */
static node_ao_time_evt_t g_tick = {0};

/** @brief A second time event; fires SIG_TOCK. */
static node_ao_time_evt_t g_tock = {0};

/** @brief One more time event than an AO may own. */
static node_ao_time_evt_t g_many[NODE_AO_MAX_TIME_EVTS + 1] = {0};

/** @brief Time events dispatched since setUp(), in dispatch order; the init
 *         event and command events are not recorded. */
static recorded_evt_t g_recorded[MAX_RECORDED] = {0};

/** @brief Number of valid entries in g_recorded. */
static size_t g_recorded_count = 0;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Dispatch function of the test AO: records SIG_TICK, SIG_TOCK and
 *        SIG_EVENT, and handles the re-arm and disarm commands.
 *
 * @param me The test AO.
 * @param e  The event.
 */
static void handle_event(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Start the test AO and run it until idle, which dispatches its init
 *        event and any time event already due. Fails the test on error.
 */
static void start_ao(void);

/**
 * @brief Set up g_tick on the test AO and arm it, before the AO starts.
 *
 * @param delay_ms  Delay until it first fires, in ms.
 * @param period_ms Period, in ms; 0 for one-shot.
 */
static void arm_tick(uint32_t delay_ms, uint32_t period_ms);

/**
 * @brief Advance the mock clock and run the AO until idle.
 *
 * @param ms Time to advance, in ms.
 */
static void advance_and_run(uint32_t ms);

/**
 * @brief Post a command or plain event to the test AO and run it until idle.
 *
 * @param evt  Event to post.
 * @param size Size of @p evt in bytes.
 */
static void post_and_run(const void *evt, size_t size);

/**
 * @brief Number of recorded events with signal @p sig.
 *
 * @param sig Signal to count.
 * @return The count.
 */
static size_t count_of(node_ao_sig_t sig);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  g_ao = (node_ao_t){0};
  g_other_ao = (node_ao_t){0};
  g_tick = (node_ao_time_evt_t){0};
  g_tock = (node_ao_time_evt_t){0};
  (void)memset(g_many, 0, sizeof(g_many));
  (void)memset(g_recorded, 0, sizeof(g_recorded));
  g_recorded_count = 0;
}

void tearDown(void) {}

/* ---- One-shot time events ---- */

/* AC: specs/14-active-objects.md #3 */
void test_one_shot_time_event_does_not_fire_before_its_delay(void) {
  arm_tick(PERIOD_MS, 0);
  start_ao();

  advance_and_run(PERIOD_MS - 1);

  TEST_ASSERT_EQUAL_size_t(0, count_of(SIG_TICK));
}

/* AC: specs/14-active-objects.md #3 */
void test_one_shot_time_event_fires_once_when_its_delay_has_passed(void) {
  arm_tick(PERIOD_MS, 0);
  start_ao();

  advance_and_run(PERIOD_MS);
  advance_and_run(10 * PERIOD_MS);

  TEST_ASSERT_EQUAL_size_t(1, count_of(SIG_TICK));
  TEST_ASSERT_EQUAL_UINT32(PERIOD_MS, g_recorded[0].at_ms);
  TEST_ASSERT_FALSE(node_ao_time_evt_is_armed(&g_tick));
}

/* AC: specs/14-active-objects.md #3 */
void test_time_event_with_zero_delay_fires_at_the_next_turn(void) {
  arm_tick(0, 0);

  start_ao();

  TEST_ASSERT_EQUAL_size_t(1, count_of(SIG_TICK));
  TEST_ASSERT_EQUAL_UINT32(0, g_recorded[0].at_ms);
}

/* ---- Periodic time events ---- */

/* AC: specs/14-active-objects.md #3 */
void test_periodic_time_event_fires_once_per_period(void) {
  const size_t periods = 3;
  arm_tick(PERIOD_MS, PERIOD_MS);
  start_ao();

  for (size_t i = 0; i < periods; i++) {
    advance_and_run(PERIOD_MS - 1);
    advance_and_run(1);
  }

  TEST_ASSERT_EQUAL_size_t(periods, count_of(SIG_TICK));
  for (size_t i = 0; i < periods; i++) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((i + 1) * PERIOD_MS, g_recorded[i].at_ms,
                                     "fired at the wrong time");
  }
}

/* AC: specs/14-active-objects.md #3 */
void test_periodic_time_event_that_falls_behind_fires_once_instead_of_in_a_burst(
    void) {
  /* 350 ms late is three and a half periods behind. */
  const uint32_t late_ms = 3 * PERIOD_MS + PERIOD_MS / 2;
  arm_tick(PERIOD_MS, PERIOD_MS);
  start_ao();

  advance_and_run(late_ms);
  size_t fired_when_late = count_of(SIG_TICK);
  advance_and_run(PERIOD_MS - 1);
  size_t fired_before_next_period = count_of(SIG_TICK);
  advance_and_run(1);

  TEST_ASSERT_EQUAL_size_t(1, fired_when_late);
  TEST_ASSERT_EQUAL_size_t(1, fired_before_next_period);
  TEST_ASSERT_EQUAL_size_t(2, count_of(SIG_TICK));
  TEST_ASSERT_EQUAL_UINT32(late_ms + PERIOD_MS, g_recorded[1].at_ms);
}

/* ---- Millisecond wrap-around ---- */

/* AC: specs/14-active-objects.md #3 */
void test_time_event_fires_on_time_across_the_millisecond_counter_wrap_around(
    void) {
  /* Armed 50 ms before the 32-bit counter wraps, so it is due 50 ms after. */
  const uint32_t start_ms = UINT32_MAX - (PERIOD_MS / 2 - 1);
  node_hal_time_mock_set(start_ms);
  arm_tick(PERIOD_MS, 0);
  start_ao();

  advance_and_run(PERIOD_MS - 1);
  size_t fired_early = count_of(SIG_TICK);
  advance_and_run(1);

  TEST_ASSERT_EQUAL_size_t(0, fired_early);
  TEST_ASSERT_EQUAL_size_t(1, count_of(SIG_TICK));
  TEST_ASSERT_EQUAL_UINT32(start_ms + PERIOD_MS, g_recorded[0].at_ms);
}

/* AC: specs/14-active-objects.md #3 */
void test_periodic_time_event_keeps_its_period_across_the_millisecond_counter_wrap_around(
    void) {
  /* The second of three firings falls just after the counter wraps. */
  const uint32_t start_ms = UINT32_MAX - (PERIOD_MS + PERIOD_MS / 2 - 1);
  const size_t periods = 3;
  node_hal_time_mock_set(start_ms);
  arm_tick(PERIOD_MS, PERIOD_MS);
  start_ao();

  for (size_t i = 0; i < periods; i++) {
    advance_and_run(PERIOD_MS - 1);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(i, count_of(SIG_TICK), "fired early");
    advance_and_run(1);
  }

  TEST_ASSERT_EQUAL_size_t(periods, count_of(SIG_TICK));
  for (size_t i = 0; i < periods; i++) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(start_ms + (uint32_t)(i + 1) * PERIOD_MS,
                                     g_recorded[i].at_ms,
                                     "fired at the wrong time");
  }
}

/* ---- Stopping and restarting time events ---- */

/* AC: specs/14-active-objects.md #3 */
void test_disarmed_time_event_does_not_fire(void) {
  arm_tick(PERIOD_MS, PERIOD_MS);
  start_ao();
  const node_ao_evt_t disarm = {.sig = SIG_DISARM};

  post_and_run(&disarm, sizeof(disarm));
  advance_and_run(10 * PERIOD_MS);

  TEST_ASSERT_EQUAL_size_t(0, count_of(SIG_TICK));
  TEST_ASSERT_FALSE(node_ao_time_evt_is_armed(&g_tick));
}

/* AC: specs/14-active-objects.md #3 */
void test_rearming_a_time_event_restarts_its_countdown(void) {
  const uint32_t rearm_after_ms = PERIOD_MS / 2;
  arm_tick(PERIOD_MS, 0);
  start_ao();
  advance_and_run(rearm_after_ms);
  const rearm_evt_t rearm = {
      .super = {.sig = SIG_REARM}, .delay_ms = PERIOD_MS, .period_ms = 0};

  post_and_run(&rearm, sizeof(rearm));
  advance_and_run(PERIOD_MS - 1);
  size_t fired_at_old_deadline = count_of(SIG_TICK);
  advance_and_run(1);

  TEST_ASSERT_EQUAL_size_t(0, fired_at_old_deadline);
  TEST_ASSERT_EQUAL_size_t(1, count_of(SIG_TICK));
  TEST_ASSERT_EQUAL_UINT32(rearm_after_ms + PERIOD_MS, g_recorded[0].at_ms);
}

/* ---- Order of time events ---- */

/* AC: specs/14-active-objects.md #3 */
void test_due_time_events_fire_after_the_event_being_handled(void) {
  arm_tick(PERIOD_MS, 0);
  start_ao();
  node_hal_time_mock_advance(PERIOD_MS);
  const node_ao_evt_t event = {.sig = SIG_EVENT};

  post_and_run(&event, sizeof(event));

  TEST_ASSERT_EQUAL_size_t(2, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(SIG_EVENT, g_recorded[0].sig);
  TEST_ASSERT_EQUAL_UINT16(SIG_TICK, g_recorded[1].sig);
}

/* AC: specs/14-active-objects.md #3 */
void test_due_time_events_fire_in_the_order_they_were_set_up(void) {
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_init(&g_tick, &g_ao, SIG_TICK));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_init(&g_tock, &g_ao, SIG_TOCK));
  /* Armed in the opposite order, to show arming order does not count. */
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_arm(&g_tock, PERIOD_MS, 0));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_arm(&g_tick, PERIOD_MS, 0));
  start_ao();

  advance_and_run(PERIOD_MS);

  TEST_ASSERT_EQUAL_size_t(2, g_recorded_count);
  TEST_ASSERT_EQUAL_UINT16(SIG_TICK, g_recorded[0].sig);
  TEST_ASSERT_EQUAL_UINT16(SIG_TOCK, g_recorded[1].sig);
}

/* ---- Setup rules ---- */

/* AC: specs/14-active-objects.md #3 */
void test_time_event_delay_longer_than_24_days_is_rejected(void) {
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_init(&g_tick, &g_ao, SIG_TICK));

  node_ao_err_t too_long_delay =
      node_ao_time_evt_arm(&g_tick, MAX_DELAY_MS + 1, 0);
  node_ao_err_t too_long_period =
      node_ao_time_evt_arm(&g_tick, PERIOD_MS, MAX_DELAY_MS + 1);
  bool armed_after_rejection = node_ao_time_evt_is_armed(&g_tick);
  node_ao_err_t longest_allowed =
      node_ao_time_evt_arm(&g_tick, MAX_DELAY_MS, MAX_DELAY_MS);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, too_long_delay);
  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, too_long_period);
  TEST_ASSERT_FALSE(armed_after_rejection);
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, longest_allowed);
}

/* AC: specs/14-active-objects.md #3 */
void test_active_object_cannot_own_more_than_eight_time_events(void) {
  for (size_t i = 0; i < NODE_AO_MAX_TIME_EVTS; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                          node_ao_time_evt_init(&g_many[i], &g_ao, SIG_TICK));
  }

  node_ao_err_t err =
      node_ao_time_evt_init(&g_many[NODE_AO_MAX_TIME_EVTS], &g_ao, SIG_TICK);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_FULL, err);
}

/* AC: specs/14-active-objects.md #3 */
void test_time_event_cannot_be_given_to_a_second_active_object(void) {
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_init(&g_tick, &g_ao, SIG_TICK));

  node_ao_err_t err = node_ao_time_evt_init(&g_tick, &g_other_ao, SIG_TICK);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_STATE, err);
}

/* AC: specs/14-active-objects.md #3 */
void test_time_event_cannot_use_the_reserved_init_signal(void) {
  node_ao_err_t err = node_ao_time_evt_init(&g_tick, &g_ao, NODE_AO_SIG_INIT);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_ARG, err);
}

/* AC: specs/14-active-objects.md #3 */
void test_time_event_that_is_not_set_up_cannot_be_armed(void) {
  node_ao_err_t err = node_ao_time_evt_arm(&g_tick, PERIOD_MS, 0);

  TEST_ASSERT_EQUAL_INT(NODE_AO_ERR_STATE, err);
  TEST_ASSERT_FALSE(node_ao_time_evt_is_armed(&g_tick));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_one_shot_time_event_does_not_fire_before_its_delay);
  RUN_TEST(test_one_shot_time_event_fires_once_when_its_delay_has_passed);
  RUN_TEST(test_time_event_with_zero_delay_fires_at_the_next_turn);
  RUN_TEST(test_periodic_time_event_fires_once_per_period);
  RUN_TEST(
      test_periodic_time_event_that_falls_behind_fires_once_instead_of_in_a_burst);
  RUN_TEST(
      test_time_event_fires_on_time_across_the_millisecond_counter_wrap_around);
  RUN_TEST(
      test_periodic_time_event_keeps_its_period_across_the_millisecond_counter_wrap_around);
  RUN_TEST(test_disarmed_time_event_does_not_fire);
  RUN_TEST(test_rearming_a_time_event_restarts_its_countdown);
  RUN_TEST(test_due_time_events_fire_after_the_event_being_handled);
  RUN_TEST(test_due_time_events_fire_in_the_order_they_were_set_up);
  RUN_TEST(test_time_event_delay_longer_than_24_days_is_rejected);
  RUN_TEST(test_active_object_cannot_own_more_than_eight_time_events);
  RUN_TEST(test_time_event_cannot_be_given_to_a_second_active_object);
  RUN_TEST(test_time_event_cannot_use_the_reserved_init_signal);
  RUN_TEST(test_time_event_that_is_not_set_up_cannot_be_armed);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void handle_event(node_ao_t *me, const node_ao_evt_t *e) {
  (void)me;
  switch (e->sig) {
    case SIG_TICK:
    case SIG_TOCK:
    case SIG_EVENT:
      if (g_recorded_count >= MAX_RECORDED) {
        TEST_FAIL_MESSAGE("more events than the recorder holds");
      }
      g_recorded[g_recorded_count++] =
          (recorded_evt_t){.sig = e->sig, .at_ms = node_hal_time_ms()};
      break;
    case SIG_REARM: {
      const rearm_evt_t *rearm = (const rearm_evt_t *)e;
      TEST_ASSERT_EQUAL_INT(
          NODE_AO_OK,
          node_ao_time_evt_arm(&g_tick, rearm->delay_ms, rearm->period_ms));
      break;
    }
    case SIG_DISARM:
      TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_time_evt_disarm(&g_tick));
      break;
    default:
      break;
  }
}

static void start_ao(void) {
  const node_ao_config_t cfg = {
      .name = "test_ao",
      .priority = 1,
      .stack_bytes = 2048,
      .queue_len = QUEUE_LEN,
      .evt_size = sizeof(rearm_evt_t),
  };
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_start(&g_ao, handle_event, &cfg));
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static void arm_tick(uint32_t delay_ms, uint32_t period_ms) {
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_init(&g_tick, &g_ao, SIG_TICK));
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK,
                        node_ao_time_evt_arm(&g_tick, delay_ms, period_ms));
}

static void advance_and_run(uint32_t ms) {
  node_hal_time_mock_advance(ms);
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static void post_and_run(const void *evt, size_t size) {
  TEST_ASSERT_EQUAL_INT(NODE_AO_OK, node_ao_post(&g_ao, evt, size));
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static size_t count_of(node_ao_sig_t sig) {
  size_t count = 0;
  for (size_t i = 0; i < g_recorded_count; i++) {
    if (g_recorded[i].sig == sig) {
      count++;
    }
  }
  return count;
}
