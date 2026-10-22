#include <stdint.h>

#include "node_hal_mock.h"
#include "unity.h"

#define I2C_PORT 0
#define SDA_PIN 32
#define SCL_PIN 33
#define BUS_FREQ_HZ 100000
#define TIMEOUT_MS 50
#define SENSOR_ADDR 0x70
#define OTHER_ADDR 0x44
#define FIRST_INVALID_ADDR 0x80
#define DEVICE_POOL_SIZE 8

/* A sensor command and a 3-byte answer; the values only need to be distinct. */
static const uint8_t measure_command[] = {0x78, 0x66};
static const uint8_t sensor_answer[] = {0x66, 0x66, 0x93};

static node_hal_i2c_bus_t g_bus = NULL;

static node_hal_i2c_dev_t add_device(uint8_t addr7) {
  node_hal_i2c_dev_t dev = NULL;
  TEST_ASSERT_EQUAL_INT(NODE_HAL_OK, node_hal_i2c_dev_add(g_bus, addr7, &dev));
  return dev;
}

void setUp(void) {
  node_hal_mock_reset();
  g_bus = NULL;
  TEST_ASSERT_EQUAL_INT(
      NODE_HAL_OK,
      node_hal_i2c_bus_init(I2C_PORT, SDA_PIN, SCL_PIN, BUS_FREQ_HZ, &g_bus));
}

void tearDown(void) {}

/* ---- Scripted sensor conversations ---- */

/* AC: specs/02-hal.md #3 */
void test_scripted_write_then_read_returns_the_queued_sensor_bytes(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, measure_command,
                                 sizeof measure_command);
  node_hal_i2c_mock_queue_read(SENSOR_ADDR, sensor_answer,
                               sizeof sensor_answer);
  uint8_t answer[sizeof sensor_answer] = {0};

  node_hal_err_t err =
      node_hal_i2c_write_read(sensor, measure_command, sizeof measure_command,
                              answer, sizeof answer, TIMEOUT_MS);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_OK, err);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(sensor_answer, answer, sizeof sensor_answer);
  TEST_ASSERT_NULL(node_hal_i2c_mock_failure());
}

/* AC: specs/02-hal.md #3 */
void test_write_with_unexpected_bytes_fails_and_explains_the_mismatch(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);
  const uint8_t wrong_command[] = {0x00, 0x00};
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, measure_command,
                                 sizeof measure_command);

  node_hal_err_t err = node_hal_i2c_write(sensor, wrong_command,
                                          sizeof wrong_command, TIMEOUT_MS);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_IO, err);
  TEST_ASSERT_EQUAL_STRING(
      "transfer to 0x70: written bytes differ from the script",
      node_hal_i2c_mock_failure());
}

/* AC: specs/02-hal.md #3 */
void test_transfer_to_the_wrong_address_fails_and_explains_the_mismatch(void) {
  node_hal_i2c_dev_t other = add_device(OTHER_ADDR);
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, measure_command,
                                 sizeof measure_command);

  node_hal_err_t err = node_hal_i2c_write(other, measure_command,
                                          sizeof measure_command, TIMEOUT_MS);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_IO, err);
  TEST_ASSERT_EQUAL_STRING(
      "transfer to 0x44: next scripted step is for another address",
      node_hal_i2c_mock_failure());
}

/* AC: specs/02-hal.md #3 */
void test_transfer_fails_when_the_script_has_no_step_left(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);

  node_hal_err_t err = node_hal_i2c_write(sensor, measure_command,
                                          sizeof measure_command, TIMEOUT_MS);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_IO, err);
  TEST_ASSERT_EQUAL_STRING("transfer to 0x70: no scripted step left",
                           node_hal_i2c_mock_failure());
}

/* AC: specs/02-hal.md #3 */
void test_queued_error_is_returned_by_the_next_transfer_to_that_device(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);
  node_hal_i2c_mock_queue_error(SENSOR_ADDR, NODE_HAL_ERR_NACK);
  uint8_t answer[sizeof sensor_answer] = {0};

  node_hal_err_t err =
      node_hal_i2c_write_read(sensor, measure_command, sizeof measure_command,
                              answer, sizeof answer, TIMEOUT_MS);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_NACK, err);
  TEST_ASSERT_EQUAL_size_t(0, node_hal_i2c_mock_pending_steps());
  TEST_ASSERT_NULL(node_hal_i2c_mock_failure());
}

/* AC: specs/02-hal.md #3 */
void test_script_reports_steps_that_were_never_used(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, measure_command,
                                 sizeof measure_command);
  node_hal_i2c_mock_queue_read(SENSOR_ADDR, sensor_answer,
                               sizeof sensor_answer);

  node_hal_i2c_write(sensor, measure_command, sizeof measure_command,
                     TIMEOUT_MS);

  TEST_ASSERT_EQUAL_size_t(1, node_hal_i2c_mock_pending_steps());
}

/* AC: specs/02-hal.md #3 */
void test_probe_finds_only_devices_marked_present(void) {
  node_hal_i2c_mock_set_present(SENSOR_ADDR, true);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_OK,
                        node_hal_i2c_probe(g_bus, SENSOR_ADDR, TIMEOUT_MS));
  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_NACK,
                        node_hal_i2c_probe(g_bus, OTHER_ADDR, TIMEOUT_MS));
}

/* AC: specs/02-hal.md #3 */
void test_reset_clears_the_script_and_any_recorded_failure(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, measure_command,
                                 sizeof measure_command);
  node_hal_i2c_mock_expect_write(SENSOR_ADDR, measure_command,
                                 sizeof measure_command);
  const uint8_t wrong_command[] = {0x00};
  node_hal_i2c_write(sensor, wrong_command, sizeof wrong_command, TIMEOUT_MS);

  node_hal_mock_reset();

  TEST_ASSERT_EQUAL_size_t(0, node_hal_i2c_mock_pending_steps());
  TEST_ASSERT_NULL(node_hal_i2c_mock_failure());
}

/* ---- Rejecting invalid requests ---- */

/* AC: specs/02-hal.md #4 */
void test_bus_setup_with_an_invalid_port_pins_or_frequency_is_rejected(void) {
  const int unused_port = 1;
  const int invalid_port = 2;
  const int invalid_pin = 40;
  node_hal_i2c_bus_t other = NULL;

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_HAL_ERR_ARG,
                                node_hal_i2c_bus_init(invalid_port, SDA_PIN,
                                                      SCL_PIN, BUS_FREQ_HZ,
                                                      &other),
                                "port");
  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_HAL_ERR_ARG,
                                node_hal_i2c_bus_init(unused_port, invalid_pin,
                                                      SCL_PIN, BUS_FREQ_HZ,
                                                      &other),
                                "data pin");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      NODE_HAL_ERR_ARG,
      node_hal_i2c_bus_init(unused_port, SDA_PIN, SDA_PIN, BUS_FREQ_HZ, &other),
      "same pin for data and clock");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      NODE_HAL_ERR_ARG,
      node_hal_i2c_bus_init(unused_port, SDA_PIN, SCL_PIN, 0, &other),
      "frequency");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      NODE_HAL_ERR_ARG,
      node_hal_i2c_bus_init(unused_port, SDA_PIN, SCL_PIN, BUS_FREQ_HZ, NULL),
      "output handle");
}

/* AC: specs/02-hal.md #4 */
void test_same_bus_port_cannot_be_set_up_twice(void) {
  node_hal_i2c_bus_t again = NULL;

  node_hal_err_t err =
      node_hal_i2c_bus_init(I2C_PORT, SDA_PIN, SCL_PIN, BUS_FREQ_HZ, &again);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG, err);
}

/* AC: specs/02-hal.md #4 */
void test_device_address_above_7_bits_is_rejected(void) {
  node_hal_i2c_dev_t dev = NULL;

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG,
                        node_hal_i2c_dev_add(g_bus, FIRST_INVALID_ADDR, &dev));
  TEST_ASSERT_EQUAL_INT(
      NODE_HAL_ERR_ARG,
      node_hal_i2c_probe(g_bus, FIRST_INVALID_ADDR, TIMEOUT_MS));
}

/* AC: specs/02-hal.md #4 */
void test_ninth_device_is_rejected_when_the_device_pool_is_full(void) {
  for (uint8_t i = 0; i < DEVICE_POOL_SIZE; i++) {
    add_device(OTHER_ADDR + i);
  }
  node_hal_i2c_dev_t ninth = NULL;

  node_hal_err_t err = node_hal_i2c_dev_add(g_bus, SENSOR_ADDR, &ninth);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG, err);
}

/* AC: specs/02-hal.md #4 */
void test_transfer_with_a_missing_buffer_or_zero_length_is_rejected(void) {
  node_hal_i2c_dev_t sensor = add_device(SENSOR_ADDR);
  uint8_t answer[sizeof sensor_answer];

  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_HAL_ERR_ARG,
                                node_hal_i2c_write(sensor, NULL, 1, TIMEOUT_MS),
                                "write without buffer");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      NODE_HAL_ERR_ARG,
      node_hal_i2c_write(sensor, measure_command, 0, TIMEOUT_MS),
      "write of zero bytes");
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      NODE_HAL_ERR_ARG,
      node_hal_i2c_write_read(sensor, measure_command, sizeof measure_command,
                              NULL, sizeof answer, TIMEOUT_MS),
      "read without buffer");
  TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_HAL_ERR_ARG,
                                node_hal_i2c_write_read(sensor, measure_command,
                                                        sizeof measure_command,
                                                        answer, 0, TIMEOUT_MS),
                                "read of zero bytes");
  TEST_ASSERT_EQUAL_size_t(0, node_hal_i2c_mock_pending_steps());
  TEST_ASSERT_NULL(node_hal_i2c_mock_failure());
}

/* AC: specs/02-hal.md #4 */
void test_transfer_on_an_unknown_device_handle_is_rejected(void) {
  node_hal_i2c_dev_t never_added = NULL;

  node_hal_err_t err = node_hal_i2c_write(never_added, measure_command,
                                          sizeof measure_command, TIMEOUT_MS);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG, err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_scripted_write_then_read_returns_the_queued_sensor_bytes);
  RUN_TEST(test_write_with_unexpected_bytes_fails_and_explains_the_mismatch);
  RUN_TEST(test_transfer_to_the_wrong_address_fails_and_explains_the_mismatch);
  RUN_TEST(test_transfer_fails_when_the_script_has_no_step_left);
  RUN_TEST(test_queued_error_is_returned_by_the_next_transfer_to_that_device);
  RUN_TEST(test_script_reports_steps_that_were_never_used);
  RUN_TEST(test_probe_finds_only_devices_marked_present);
  RUN_TEST(test_reset_clears_the_script_and_any_recorded_failure);
  RUN_TEST(test_bus_setup_with_an_invalid_port_pins_or_frequency_is_rejected);
  RUN_TEST(test_same_bus_port_cannot_be_set_up_twice);
  RUN_TEST(test_device_address_above_7_bits_is_rejected);
  RUN_TEST(test_ninth_device_is_rejected_when_the_device_pool_is_full);
  RUN_TEST(test_transfer_with_a_missing_buffer_or_zero_length_is_rejected);
  RUN_TEST(test_transfer_on_an_unknown_device_handle_is_rejected);
  return UNITY_END();
}
