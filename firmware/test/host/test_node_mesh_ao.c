/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_ao_port_mock.h"
#include "node_hal_mock.h"
#include "node_mesh_err.h"
#include "node_mesh_internal.h"
#include "node_mesh_log_mock.h"
#include "node_mesh_port_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Elements registered: the primary element only (spec 10). */
#define ELEMENT_COUNT 1

/** @brief Unicast address the gateway gives the primary element. */
#define PRIMARY_ADDR 0x0005

/** @brief Index of the network key the gateway gives the node. */
#define NET_IDX 0x000

/** @brief IV index of the network. */
#define IV_INDEX 7u

/** @brief An ESP-BLE-MESH error code for a failed beacon start; any
 *         non-zero value works. */
#define BEACON_START_ERR (-5)

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Start the mesh AO with ELEMENT_COUNT and run its
 *        init event. Fails the test if it does not start.
 */
static void start_mesh_task(void);

/**
 * @brief Report that the gateway provisioned the node, with relay on, and
 *        run the mesh AO until idle. Fails the test if the event is lost.
 */
static void provisioning_completes(void);

/**
 * @brief Dispatch queued events until no AO has any left. Fails the test if
 *        the AOs never go idle.
 */
static void run_until_idle(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_mesh_ao_reset();
  node_mesh_port_mock_reset();
  node_mesh_log_mock_reset();
}

void tearDown(void) {}

/* ---- Provisioning complete ---- */

/* AC: specs/10-mesh-node-core.md #2 */
void test_completing_provisioning_logs_the_address_element_count_and_relay_state(
    void) {
  start_mesh_task();

  provisioning_completes();

  const node_mesh_log_mock_prov_t logged =
      node_mesh_log_mock_last_prov_complete();
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_prov_complete_count());
  TEST_ASSERT_EQUAL_HEX16(PRIMARY_ADDR, logged.addr);
  TEST_ASSERT_EQUAL_UINT8(ELEMENT_COUNT, logged.element_count);
  TEST_ASSERT_TRUE(logged.relay);
  TEST_ASSERT_EQUAL_HEX16(NET_IDX, logged.net_idx);
  TEST_ASSERT_EQUAL_UINT32(IV_INDEX, logged.iv_index);
}

/* ---- Reset by the gateway ---- */

/* AC: specs/10-mesh-node-core.md #3 */
void test_reset_by_the_gateway_restarts_the_node(void) {
  start_mesh_task();

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_ao_post_node_reset());
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_port_mock_restart_count());
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_node_reset_count());
}

/* ---- Provisioning progress ---- */

void test_opening_a_provisioning_link_is_logged(void) {
  start_mesh_task();

  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_OK, node_mesh_ao_post_notice(NODE_MESH_AO_NOTICE_LINK_OPEN, 0));
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_notice_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_AO_NOTICE_LINK_OPEN,
                        node_mesh_log_mock_last_notice());
}

void test_closing_a_provisioning_link_is_logged(void) {
  start_mesh_task();

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_ao_post_notice(
                                          NODE_MESH_AO_NOTICE_LINK_CLOSE, 0));
  run_until_idle();

  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_notice_count());
  TEST_ASSERT_EQUAL_INT(NODE_MESH_AO_NOTICE_LINK_CLOSE,
                        node_mesh_log_mock_last_notice());
}

/* AC: specs/10-mesh-node-core.md #1 */
void test_a_failure_to_start_unprovisioned_beacons_is_logged_with_its_error(
    void) {
  start_mesh_task();

  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_OK,
      node_mesh_ao_post_notice(NODE_MESH_AO_NOTICE_PROV_ENABLE_FAILED,
                               BEACON_START_ERR));
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_MESH_AO_NOTICE_PROV_ENABLE_FAILED,
                        node_mesh_log_mock_last_notice());
  TEST_ASSERT_EQUAL_INT32(BEACON_START_ERR,
                          node_mesh_log_mock_last_notice_err());
}

/* ---- Delivering mesh events ---- */

void test_mesh_events_sent_before_the_mesh_task_starts_are_reported_as_lost(
    void) {
  node_mesh_err_t err =
      node_mesh_ao_post_prov_complete(NET_IDX, PRIMARY_ADDR, IV_INDEX, true);
  run_until_idle();

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_STATE, err);
  TEST_ASSERT_EQUAL_size_t(1, node_mesh_log_mock_post_failed_count());
  TEST_ASSERT_EQUAL_size_t(0, node_mesh_log_mock_prov_complete_count());
}

void test_mesh_events_are_reported_as_lost_when_the_mesh_task_queue_is_full(
    void) {
  start_mesh_task();
  size_t lost = 0;

  /* More events than any queue the host port can hold, with none
   * dispatched in between. */
  for (size_t i = 0; i <= NODE_AO_PORT_MOCK_MAX_QUEUE_LEN; i++) {
    if (node_mesh_ao_post_notice(NODE_MESH_AO_NOTICE_LINK_OPEN, 0) !=
        NODE_MESH_OK) {
      lost++;
    }
  }

  TEST_ASSERT_GREATER_THAN_size_t(0, lost);
  TEST_ASSERT_EQUAL_size_t(lost, node_mesh_log_mock_post_failed_count());
}

void test_mesh_task_cannot_be_started_twice(void) {
  start_mesh_task();

  node_mesh_err_t err = node_mesh_ao_start(ELEMENT_COUNT);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_STATE, err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_completing_provisioning_logs_the_address_element_count_and_relay_state);
  RUN_TEST(test_reset_by_the_gateway_restarts_the_node);
  RUN_TEST(test_opening_a_provisioning_link_is_logged);
  RUN_TEST(test_closing_a_provisioning_link_is_logged);
  RUN_TEST(
      test_a_failure_to_start_unprovisioned_beacons_is_logged_with_its_error);
  RUN_TEST(
      test_mesh_events_sent_before_the_mesh_task_starts_are_reported_as_lost);
  RUN_TEST(
      test_mesh_events_are_reported_as_lost_when_the_mesh_task_queue_is_full);
  RUN_TEST(test_mesh_task_cannot_be_started_twice);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void start_mesh_task(void) {
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_ao_start(ELEMENT_COUNT));
  run_until_idle();
}

static void provisioning_completes(void) {
  TEST_ASSERT_EQUAL_INT(
      NODE_MESH_OK,
      node_mesh_ao_post_prov_complete(NET_IDX, PRIMARY_ADDR, IV_INDEX, true));
  run_until_idle();
}

static void run_until_idle(void) {
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}
