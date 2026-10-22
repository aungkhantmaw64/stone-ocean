#pragma once

/**
 * @file node_hal_mock.h
 * @brief Test controls for the host implementation of node_hal.h.
 *
 * The mock keeps all state in static memory. Call node_hal_mock_reset() from
 * every test's setUp() so tests stay independent.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_hal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Number of GPIO numbers the mock accepts: 0 to 39, as on ESP32. */
#define NODE_HAL_MOCK_GPIO_COUNT 40

/** @brief Maximum number of scripted I2C steps queued at once. */
#define NODE_HAL_MOCK_I2C_MAX_STEPS 32

/** @brief Maximum number of bytes in one scripted I2C write or read. */
#define NODE_HAL_MOCK_I2C_MAX_DATA 32

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/** @brief Clear all GPIO, I2C and time state. */
void node_hal_mock_reset(void);

/** @brief Clear all GPIO state: pins unconfigured, levels low, no callbacks,
 *         no write counts and no pending write failure. */
void node_hal_gpio_mock_reset(void);

/**
 * @brief Set the level seen on an input pin, as if driven from outside.
 *
 * If the level changes and the pin has a callback for a matching edge, the
 * callback runs synchronously before this function returns. Out-of-range pins
 * are ignored.
 *
 * @param pin   GPIO number.
 * @param level 0 for low, any other value for high.
 */
void node_hal_gpio_mock_set_input(int pin, int level);

/**
 * @brief Number of successful node_hal_gpio_write() calls to a pin since the
 *        last reset.
 *
 * @param pin GPIO number.
 * @return The count; 0 for an out-of-range pin.
 */
size_t node_hal_gpio_mock_write_count(int pin);

/**
 * @brief Make the next node_hal_gpio_write() to a valid output pin fail.
 *
 * The failing write leaves the pin's level unchanged and is not counted by
 * node_hal_gpio_mock_write_count().
 *
 * @param err Error the write returns, for example NODE_HAL_ERR_IO.
 */
void node_hal_gpio_mock_fail_next_write(node_hal_err_t err);

/**
 * @brief Clear all I2C state: buses, devices, the script, present addresses
 *        and any recorded failure.
 */
void node_hal_i2c_mock_reset(void);

/**
 * @brief Expect the next transfer to @p addr7 to write exactly these bytes.
 *
 * Steps are consumed in the order they are queued. The write half of
 * node_hal_i2c_write_read() consumes a write step too.
 *
 * @param addr7 7-bit device address.
 * @param data  Expected bytes.
 * @param len   Number of bytes, at most NODE_HAL_MOCK_I2C_MAX_DATA.
 */
void node_hal_i2c_mock_expect_write(uint8_t addr7, const uint8_t *data,
                                    size_t len);

/**
 * @brief Queue the bytes returned by the read half of the next
 *        node_hal_i2c_write_read() to @p addr7.
 *
 * @param addr7 7-bit device address.
 * @param data  Bytes to return.
 * @param len   Number of bytes; must equal the length the caller reads.
 */
void node_hal_i2c_mock_queue_read(uint8_t addr7, const uint8_t *data,
                                  size_t len);

/**
 * @brief Make the next transfer to @p addr7 fail with @p err.
 *
 * Takes the place of a write step: the failing transfer consumes this step
 * and nothing else.
 *
 * @param addr7 7-bit device address.
 * @param err   Error the transfer returns, for example NODE_HAL_ERR_NACK.
 */
void node_hal_i2c_mock_queue_error(uint8_t addr7, node_hal_err_t err);

/**
 * @brief Set whether node_hal_i2c_probe() finds a device at @p addr7.
 *
 * Only affects probing; transfers follow the script.
 *
 * @param addr7   7-bit device address.
 * @param present true if the address acknowledges.
 */
void node_hal_i2c_mock_set_present(uint8_t addr7, bool present);

/**
 * @brief Make the next node_hal_i2c_bus_init() with valid arguments fail.
 *
 * The bus is not initialised and its port stays free, so a later init of
 * the same port succeeds.
 *
 * @param err Error the init returns, for example NODE_HAL_ERR_IO.
 */
void node_hal_i2c_mock_fail_next_bus_init(node_hal_err_t err);

/**
 * @brief Describe the first transfer that did not match the script.
 *
 * A mismatching transfer returns NODE_HAL_ERR_IO to its caller.
 *
 * @return A message naming the transfer and what was expected, or NULL if
 *         every transfer so far matched.
 */
const char *node_hal_i2c_mock_failure(void);

/**
 * @brief Number of queued steps that no transfer has consumed yet.
 *
 * @return Remaining step count.
 */
size_t node_hal_i2c_mock_pending_steps(void);

/** @brief Set virtual time to 0. */
void node_hal_time_mock_reset(void);

/**
 * @brief Move virtual time forward.
 *
 * Wraps around at UINT32_MAX like the real clock.
 *
 * @param ms Time to add, in ms.
 */
void node_hal_time_mock_advance(uint32_t ms);

/**
 * @brief Set virtual time, for example just before a wrap-around.
 *
 * @param ms New time, in ms.
 */
void node_hal_time_mock_set(uint32_t ms);
