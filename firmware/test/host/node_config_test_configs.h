#pragma once

/**
 * @file node_config_test_configs.h
 * @brief JSON building blocks shared by the node_config host tests.
 *
 * Each macro expands to a string literal, so a test can write a whole config
 * inline and show only the field it is about. Pins avoid strapping pins, so
 * the building blocks never cause warnings on their own.
 */

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief A version 1 config with the given bus and device list contents. */
#define TEST_CONFIG(buses, devices)                                            \
  "{\"version\":1,\"node_name\":\"test-node\",\"i2c_buses\":[" buses           \
  "],\"devices\":[" devices "]}"

/** @brief A version 1 config with no "i2c_buses" key at all. */
#define TEST_CONFIG_WITHOUT_BUSES(devices)                                     \
  "{\"version\":1,\"node_name\":\"test-node\",\"devices\":[" devices "]}"

/** @brief I2C bus "i2c0" on port 0, SDA 21, SCL 22, 100 kHz. */
#define TEST_BUS_I2C0                                                          \
  "{\"id\":\"i2c0\",\"port\":0,\"sda\":21,\"scl\":22,\"freq_hz\":100000}"

/** @brief I2C bus "i2c1" on port 1, SDA 18, SCL 19, 100 kHz. */
#define TEST_BUS_I2C1                                                          \
  "{\"id\":\"i2c1\",\"port\":1,\"sda\":18,\"scl\":19,\"freq_hz\":100000}"

/** @brief LED with the given id (string literal) and GPIO (number), only its
 *         required fields set. */
#define TEST_LED(id, gpio)                                                     \
  "{\"id\":\"" id "\",\"type\":\"led\",\"gpio\":" #gpio ",\"active_high\":"    \
                                                        "true}"

/** @brief PIR with the given id and GPIO, only its required fields set. */
#define TEST_PIR(id, gpio)                                                     \
  "{\"id\":\"" id "\",\"type\":\"pir\",\"gpio\":" #gpio ",\"active_high\":"    \
                                                        "true}"

/** @brief SHTC3 with the given id and bus id, sampling every 10 s, only its
 *         required fields set. */
#define TEST_SHTC3(id, bus)                                                    \
  "{\"id\":\"" id "\",\"type\":\"shtc3\",\"bus\":\"" bus                       \
  "\",\"sample_interval_ms\":10000}"
