/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#include "node_mesh_err.h"
#include "node_mesh_uuid.h"
#include "unity.h"

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Bluetooth MAC address of the board in these tests. */
static const uint8_t g_mac[NODE_MESH_UUID_MAC_LEN] = {0x24, 0x6f, 0x28,
                                                      0xaa, 0xbb, 0xcc};

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {}

void tearDown(void) {}

/* ---- Device UUID ---- */

/* AC: specs/10-mesh-node-core.md #1 */
void test_device_uuid_starts_with_stoneocean_and_ends_with_the_bluetooth_mac(
    void) {
  const uint8_t expected[NODE_MESH_UUID_LEN] = {
      's', 't', 'o',  'n',  'e',  'o',  'c',  'e',
      'a', 'n', 0x24, 0x6f, 0x28, 0xaa, 0xbb, 0xcc};
  uint8_t uuid[NODE_MESH_UUID_LEN] = {0};

  node_mesh_err_t err = node_mesh_uuid_build(g_mac, uuid);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, err);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, uuid, NODE_MESH_UUID_LEN);
}

/* AC: specs/10-mesh-node-core.md #1 */
void test_device_uuid_is_the_same_on_every_boot_for_the_same_mac(void) {
  uint8_t first_boot[NODE_MESH_UUID_LEN] = {0};
  uint8_t second_boot[NODE_MESH_UUID_LEN] = {0};
  /* Leftover bytes from an earlier boot must not leak into the UUID. */
  (void)memset(second_boot, 0xff, sizeof(second_boot));

  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_uuid_build(g_mac, first_boot));
  TEST_ASSERT_EQUAL_INT(NODE_MESH_OK, node_mesh_uuid_build(g_mac, second_boot));

  TEST_ASSERT_EQUAL_HEX8_ARRAY(first_boot, second_boot, NODE_MESH_UUID_LEN);
}

void test_device_uuid_without_a_mac_address_is_rejected(void) {
  uint8_t uuid[NODE_MESH_UUID_LEN] = {0};

  node_mesh_err_t err = node_mesh_uuid_build(NULL, uuid);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
}

void test_device_uuid_without_somewhere_to_put_it_is_rejected(void) {
  node_mesh_err_t err = node_mesh_uuid_build(g_mac, NULL);

  TEST_ASSERT_EQUAL_INT(NODE_MESH_ERR_ARG, err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_device_uuid_starts_with_stoneocean_and_ends_with_the_bluetooth_mac);
  RUN_TEST(test_device_uuid_is_the_same_on_every_boot_for_the_same_mac);
  RUN_TEST(test_device_uuid_without_a_mac_address_is_rejected);
  RUN_TEST(test_device_uuid_without_somewhere_to_put_it_is_rejected);
  return UNITY_END();
}
