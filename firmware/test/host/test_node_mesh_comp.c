/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "node_config.h"
#include "node_mesh_comp.h"
#include "node_mesh_err.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Elements of a node with a sensor, a motion sensor and an LED: the
 *         primary element plus one per device. */
#define THREE_DEVICE_ELEMENTS 4

/** @brief Elements of a node with the most devices allowed: the primary
 *         element plus NODE_CONFIG_MAX_DEVICES (8). */
#define MOST_DEVICE_ELEMENTS 9

/** @brief Elements when only the primary element is registered. */
#define PRIMARY_ONLY 1

/** @brief A stored hash that matches no config used in these tests. */
#define UNRELATED_HASH 0x12345678u

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief A sensor, a motion sensor and an LED, in that order. */
static const node_config_dev_type_t g_sensor_motion_led[] = {
    NODE_CONFIG_DEV_SHTC3,
    NODE_CONFIG_DEV_PIR,
    NODE_CONFIG_DEV_LED,
};

/** @brief The same three devices in the opposite order. */
static const node_config_dev_type_t g_led_motion_sensor[] = {
    NODE_CONFIG_DEV_LED,
    NODE_CONFIG_DEV_PIR,
    NODE_CONFIG_DEV_SHTC3,
};

/** @brief The three devices plus a second LED. */
static const node_config_dev_type_t g_sensor_motion_two_leds[] = {
    NODE_CONFIG_DEV_SHTC3,
    NODE_CONFIG_DEV_PIR,
    NODE_CONFIG_DEV_LED,
    NODE_CONFIG_DEV_LED,
};

/** @brief Bluetooth MAC address of the board in the UUID tests. */
static const uint8_t g_mac[NODE_MESH_COMP_MAC_LEN] = {0x24, 0x6f, 0x28,
                                                      0xaa, 0xbb, 0xcc};

/** @brief Config under test; reset in setUp(). */
static node_config_t g_config = {0};

/** @brief Plan under test; reset in setUp(). */
static node_mesh_comp_plan_t g_plan = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Fill g_config with the given devices, in order.
 *
 * @param types Device types; may be NULL if @p count is 0.
 * @param count Number of devices, at most NODE_CONFIG_MAX_DEVICES.
 */
static void use_devices(const node_config_dev_type_t *types, size_t count);

/**
 * @brief Composition hash of a config with the given devices, in order.
 *
 * @param types Device types; may be NULL if @p count is 0.
 * @param count Number of devices, at most NODE_CONFIG_MAX_DEVICES.
 * @return node_config_composition_hash() of that config.
 */
static uint32_t hash_of_devices(const node_config_dev_type_t *types,
                                size_t count);

/**
 * @brief Composition hash of a config with no devices, which stands for the
 *        primary element alone.
 *
 * @return The hash.
 */
static uint32_t hash_of_no_devices(void);

/**
 * @brief Plan the composition for g_config into g_plan and fail the test if
 *        planning fails.
 *
 * @param hash_stored True if a hash was stored at provisioning.
 * @param stored_hash The stored hash.
 */
static void plan_for_config(bool hash_stored, uint32_t stored_hash);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  (void)memset(&g_config, 0, sizeof(g_config));
  (void)memset(&g_plan, 0, sizeof(g_plan));
  g_config.version = 1;
}

void tearDown(void) {}

/* ---- Choosing the elements ---- */

/* AC: specs/10-mesh-node-core.md #4 */
void test_unprovisioned_node_gets_the_primary_element_plus_one_element_per_configured_device(
    void) {
  use_devices(g_sensor_motion_led, 3);

  plan_for_config(false, 0);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_UNPROVISIONED, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(THREE_DEVICE_ELEMENTS, g_plan.element_count);
  TEST_ASSERT_EQUAL_UINT8(3, g_plan.device_count);
}

/* AC: specs/10-mesh-node-core.md #4 */
void test_node_with_an_empty_device_list_gets_only_the_primary_element(void) {
  use_devices(NULL, 0);

  plan_for_config(false, 0);

  TEST_ASSERT_EQUAL_UINT8(PRIMARY_ONLY, g_plan.element_count);
  TEST_ASSERT_EQUAL_UINT8(0, g_plan.device_count);
}

/* AC: specs/10-mesh-node-core.md #4 */
void test_node_with_the_most_devices_allowed_gets_nine_elements(void) {
  node_config_dev_type_t types[NODE_CONFIG_MAX_DEVICES] = {0};
  for (size_t i = 0; i < NODE_CONFIG_MAX_DEVICES; i++) {
    types[i] = NODE_CONFIG_DEV_LED;
  }
  use_devices(types, NODE_CONFIG_MAX_DEVICES);

  plan_for_config(false, 0);

  TEST_ASSERT_EQUAL_UINT8(MOST_DEVICE_ELEMENTS, g_plan.element_count);
  TEST_ASSERT_EQUAL_UINT8(NODE_CONFIG_MAX_DEVICES, g_plan.device_count);
}

/* AC: specs/10-mesh-node-core.md #4 */
void test_safe_mode_registers_only_the_primary_element(void) {
  node_mesh_err_t err = node_mesh_comp_plan(NULL, false, 0, &g_plan);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_SAFE_MODE, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(PRIMARY_ONLY, g_plan.element_count);
  TEST_ASSERT_EQUAL_UINT8(0, g_plan.device_count);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_safe_mode_ignores_a_composition_hash_stored_at_provisioning(void) {
  node_mesh_err_t err =
      node_mesh_comp_plan(NULL, true, UNRELATED_HASH, &g_plan);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_SAFE_MODE, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(PRIMARY_ONLY, g_plan.element_count);
}

/* ---- Detecting a changed config ---- */

/* AC: specs/10-mesh-node-core.md #5 */
void test_provisioned_node_keeps_its_device_elements_when_the_config_composition_is_unchanged(
    void) {
  use_devices(g_sensor_motion_led, 3);

  plan_for_config(true, hash_of_devices(g_sensor_motion_led, 3));

  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_MATCH, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(THREE_DEVICE_ELEMENTS, g_plan.element_count);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_device_elements_are_disabled_when_a_device_is_added_after_provisioning(
    void) {
  use_devices(g_sensor_motion_two_leds, 4);

  plan_for_config(true, hash_of_devices(g_sensor_motion_led, 3));

  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_MISMATCH, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(PRIMARY_ONLY, g_plan.element_count);
  TEST_ASSERT_EQUAL_UINT8(0, g_plan.device_count);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_device_elements_are_disabled_when_devices_are_reordered_after_provisioning(
    void) {
  use_devices(g_led_motion_sensor, 3);

  plan_for_config(true, hash_of_devices(g_sensor_motion_led, 3));

  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_MISMATCH, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(PRIMARY_ONLY, g_plan.element_count);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_changing_only_the_intervals_keeps_the_device_elements_after_provisioning(
    void) {
  use_devices(g_sensor_motion_led, 3);
  const uint32_t hash_at_provisioning = hash_of_devices(g_sensor_motion_led, 3);
  g_config.dev[0].shtc3.sample_interval_ms = 2000;
  g_config.dev[0].shtc3.report.max_interval_ms = 120000;
  g_config.dev[1].pir.debounce_ms = 200;

  plan_for_config(true, hash_at_provisioning);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_COMP_MATCH, g_plan.outcome);
  TEST_ASSERT_EQUAL_UINT8(THREE_DEVICE_ELEMENTS, g_plan.element_count);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_a_changed_config_reports_the_active_config_hash_for_the_warning(
    void) {
  use_devices(g_sensor_motion_two_leds, 4);

  plan_for_config(true, hash_of_devices(g_sensor_motion_led, 3));

  TEST_ASSERT_EQUAL_HEX32(hash_of_devices(g_sensor_motion_two_leds, 4),
                          g_plan.config_hash);
}

/* ---- What is stored at provisioning ---- */

/* AC: specs/10-mesh-node-core.md #5 */
void test_provisioning_stores_the_config_hash_when_the_devices_have_elements(
    void) {
  use_devices(g_sensor_motion_led, 3);

  plan_for_config(false, 0);

  TEST_ASSERT_EQUAL_HEX32(hash_of_devices(g_sensor_motion_led, 3),
                          g_plan.hash_in_use);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_provisioning_stores_the_empty_device_list_hash_in_safe_mode(void) {
  node_mesh_err_t err = node_mesh_comp_plan(NULL, false, 0, &g_plan);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_HEX32(hash_of_no_devices(), g_plan.hash_in_use);
}

/* AC: specs/10-mesh-node-core.md #5 */
void test_provisioning_stores_the_empty_device_list_hash_after_a_config_change(
    void) {
  use_devices(g_sensor_motion_two_leds, 4);

  plan_for_config(true, hash_of_devices(g_sensor_motion_led, 3));

  TEST_ASSERT_EQUAL_HEX32(hash_of_no_devices(), g_plan.hash_in_use);
}

/* ---- Rejecting bad input ---- */

void test_composition_plan_is_rejected_for_a_config_with_more_devices_than_allowed(
    void) {
  g_config.n_dev = NODE_CONFIG_MAX_DEVICES + 1;

  node_mesh_err_t err = node_mesh_comp_plan(&g_config, false, 0, &g_plan);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
  TEST_ASSERT_EQUAL_UINT8(0, g_plan.element_count);
}

void test_composition_plan_is_rejected_for_a_negative_device_count(void) {
  g_config.n_dev = -1;

  node_mesh_err_t err = node_mesh_comp_plan(&g_config, false, 0, &g_plan);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
  TEST_ASSERT_EQUAL_UINT8(0, g_plan.element_count);
}

void test_composition_plan_without_somewhere_to_put_the_result_is_rejected(
    void) {
  use_devices(g_sensor_motion_led, 3);

  node_mesh_err_t err = node_mesh_comp_plan(&g_config, false, 0, NULL);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
}

/* ---- Device UUID ---- */

/* AC: specs/10-mesh-node-core.md #1 */
void test_device_uuid_starts_with_stoneocean_and_ends_with_the_bluetooth_mac(
    void) {
  const uint8_t expected[NODE_MESH_COMP_UUID_LEN] = {
      's', 't', 'o',  'n',  'e',  'o',  'c',  'e',
      'a', 'n', 0x24, 0x6f, 0x28, 0xaa, 0xbb, 0xcc};
  uint8_t uuid[NODE_MESH_COMP_UUID_LEN] = {0};

  node_mesh_err_t err = node_mesh_comp_uuid(g_mac, uuid);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, uuid, NODE_MESH_COMP_UUID_LEN);
}

/* AC: specs/10-mesh-node-core.md #1 */
void test_device_uuid_is_the_same_on_every_boot_for_the_same_mac(void) {
  uint8_t first_boot[NODE_MESH_COMP_UUID_LEN] = {0};
  uint8_t second_boot[NODE_MESH_COMP_UUID_LEN] = {0};
  /* Leftover bytes from an earlier boot must not leak into the UUID. */
  (void)memset(second_boot, 0xff, sizeof(second_boot));

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_comp_uuid(g_mac, first_boot));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_comp_uuid(g_mac, second_boot));

  TEST_ASSERT_EQUAL_HEX8_ARRAY(first_boot, second_boot,
                               NODE_MESH_COMP_UUID_LEN);
}

void test_device_uuid_without_a_mac_address_is_rejected(void) {
  uint8_t uuid[NODE_MESH_COMP_UUID_LEN] = {0};

  node_mesh_err_t err = node_mesh_comp_uuid(NULL, uuid);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
}

void test_device_uuid_without_somewhere_to_put_it_is_rejected(void) {
  node_mesh_err_t err = node_mesh_comp_uuid(g_mac, NULL);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_unprovisioned_node_gets_the_primary_element_plus_one_element_per_configured_device);
  RUN_TEST(test_node_with_an_empty_device_list_gets_only_the_primary_element);
  RUN_TEST(test_node_with_the_most_devices_allowed_gets_nine_elements);
  RUN_TEST(test_safe_mode_registers_only_the_primary_element);
  RUN_TEST(test_safe_mode_ignores_a_composition_hash_stored_at_provisioning);
  RUN_TEST(
      test_provisioned_node_keeps_its_device_elements_when_the_config_composition_is_unchanged);
  RUN_TEST(
      test_device_elements_are_disabled_when_a_device_is_added_after_provisioning);
  RUN_TEST(
      test_device_elements_are_disabled_when_devices_are_reordered_after_provisioning);
  RUN_TEST(
      test_changing_only_the_intervals_keeps_the_device_elements_after_provisioning);
  RUN_TEST(
      test_a_changed_config_reports_the_active_config_hash_for_the_warning);
  RUN_TEST(
      test_provisioning_stores_the_config_hash_when_the_devices_have_elements);
  RUN_TEST(test_provisioning_stores_the_empty_device_list_hash_in_safe_mode);
  RUN_TEST(
      test_provisioning_stores_the_empty_device_list_hash_after_a_config_change);
  RUN_TEST(
      test_composition_plan_is_rejected_for_a_config_with_more_devices_than_allowed);
  RUN_TEST(test_composition_plan_is_rejected_for_a_negative_device_count);
  RUN_TEST(
      test_composition_plan_without_somewhere_to_put_the_result_is_rejected);
  RUN_TEST(
      test_device_uuid_starts_with_stoneocean_and_ends_with_the_bluetooth_mac);
  RUN_TEST(test_device_uuid_is_the_same_on_every_boot_for_the_same_mac);
  RUN_TEST(test_device_uuid_without_a_mac_address_is_rejected);
  RUN_TEST(test_device_uuid_without_somewhere_to_put_it_is_rejected);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void use_devices(const node_config_dev_type_t *types, size_t count) {
  TEST_ASSERT_LESS_OR_EQUAL_size_t(NODE_CONFIG_MAX_DEVICES, count);
  g_config.n_dev = (int)count;
  for (size_t i = 0; i < count; i++) {
    g_config.dev[i].type = types[i];
  }
}

static uint32_t hash_of_devices(const node_config_dev_type_t *types,
                                size_t count) {
  TEST_ASSERT_LESS_OR_EQUAL_size_t(NODE_CONFIG_MAX_DEVICES, count);
  node_config_t config = {0};
  config.n_dev = (int)count;
  for (size_t i = 0; i < count; i++) {
    config.dev[i].type = types[i];
  }
  return node_config_composition_hash(&config);
}

static uint32_t hash_of_no_devices(void) { return hash_of_devices(NULL, 0); }

static void plan_for_config(bool hash_stored, uint32_t stored_hash) {
  node_mesh_err_t err =
      node_mesh_comp_plan(&g_config, hash_stored, stored_hash, &g_plan);
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
}
