#pragma once

/**
 * @file node_hal.h
 * @brief Thin hardware abstraction for GPIO, I2C and time.
 *
 * Drivers and logic reach hardware only through this header. The ESP-IDF
 * build links the ESP implementation in this component; host tests link the
 * mock in firmware/test/host/mocks/. The header must not include ESP-IDF
 * headers.
 *
 * Unless stated otherwise, functions are called from task context and are not
 * safe to call from an ISR.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of a HAL call. */
typedef enum {
  HAL_OK = 0,      /**< Success. */
  HAL_ERR_ARG,     /**< Invalid argument, or a pin that cannot be used. */
  HAL_ERR_TIMEOUT, /**< The bus operation did not finish within the timeout. */
  HAL_ERR_NACK,    /**< The I2C device did not acknowledge. */
  HAL_ERR_IO,      /**< Any other hardware or driver failure. */
} hal_err_t;

/** @brief Direction of a GPIO pin. */
typedef enum { HAL_GPIO_IN, HAL_GPIO_OUT } hal_gpio_dir_t;

/** @brief Internal pull resistor of a GPIO pin. */
typedef enum { HAL_PULL_NONE, HAL_PULL_UP, HAL_PULL_DOWN } hal_pull_t;

/** @brief Edge that triggers a GPIO interrupt. */
typedef enum { HAL_EDGE_RISING, HAL_EDGE_FALLING, HAL_EDGE_ANY } hal_edge_t;

/**
 * @brief GPIO interrupt callback.
 *
 * Runs in ISR context. It must only signal a task or queue (for example with
 * xQueueSendFromISR) and return; it must not block, log or call other HAL
 * functions.
 *
 * @param pin GPIO number that triggered the interrupt.
 * @param ctx Context pointer given to hal_gpio_set_isr().
 */
typedef void (*hal_gpio_isr_cb)(int pin, void *ctx);

/**
 * @brief Configure a GPIO pin's direction and pull resistor.
 *
 * Pins the hardware cannot use are rejected: pins that do not exist, pins
 * reserved by flash or PSRAM, output on input-only pins and pull resistors on
 * pins that have none.
 *
 * @param pin  GPIO number.
 * @param dir  Input or output.
 * @param pull Internal pull resistor to enable.
 * @return HAL_OK, HAL_ERR_ARG for an unusable pin or invalid enum value, or
 *         HAL_ERR_IO.
 */
hal_err_t hal_gpio_config(int pin, hal_gpio_dir_t dir, hal_pull_t pull);

/**
 * @brief Drive an output pin.
 *
 * @param pin   GPIO number, previously configured as output.
 * @param level 0 for low, any other value for high.
 * @return HAL_OK, HAL_ERR_ARG for an unusable pin, or HAL_ERR_IO.
 */
hal_err_t hal_gpio_write(int pin, int level);

/**
 * @brief Read the level of a pin.
 *
 * Safe to call from ISR context.
 *
 * @param pin GPIO number.
 * @return 0 or 1, or -1 if @p pin is not a valid GPIO number.
 */
int hal_gpio_read(int pin);

/**
 * @brief Call @p cb when an edge occurs on an input pin.
 *
 * The first call installs the platform's GPIO interrupt service. Calling it
 * again for the same pin replaces the edge, callback and context.
 *
 * @param pin  GPIO number, previously configured as input.
 * @param edge Edge that triggers the callback.
 * @param cb   Callback run in ISR context; must not be NULL.
 * @param ctx  Passed unchanged to @p cb; may be NULL.
 * @return HAL_OK, HAL_ERR_ARG for an unusable pin, invalid edge or NULL
 *         callback, or HAL_ERR_IO.
 */
hal_err_t hal_gpio_set_isr(int pin, hal_edge_t edge, hal_gpio_isr_cb cb,
                           void *ctx);

/** @brief Handle to an initialised I2C bus. */
typedef struct hal_i2c_bus *hal_i2c_bus_t;

/** @brief Handle to a device on an I2C bus. */
typedef struct hal_i2c_dev *hal_i2c_dev_t;

/**
 * @brief Initialise an I2C master bus.
 *
 * Call once per port during init. Handles come from a fixed pool, so no
 * memory is allocated after init.
 *
 * @param port    I2C controller number.
 * @param sda     GPIO number of the data line.
 * @param scl     GPIO number of the clock line.
 * @param freq_hz Bus clock in Hz; must be non-zero.
 * @param out     Receives the bus handle on success; must not be NULL.
 * @return HAL_OK, HAL_ERR_ARG for invalid arguments or a port already in use,
 *         or HAL_ERR_IO.
 */
hal_err_t hal_i2c_bus_init(int port, int sda, int scl, uint32_t freq_hz,
                           hal_i2c_bus_t *out);

/**
 * @brief Register a device on a bus.
 *
 * @param bus   Bus from hal_i2c_bus_init().
 * @param addr7 7-bit device address (0x00–0x7F).
 * @param out   Receives the device handle on success; must not be NULL.
 * @return HAL_OK, HAL_ERR_ARG for invalid arguments or a full device pool, or
 *         HAL_ERR_IO.
 */
hal_err_t hal_i2c_dev_add(hal_i2c_bus_t bus, uint8_t addr7, hal_i2c_dev_t *out);

/**
 * @brief Write bytes to a device in one transaction.
 *
 * Blocks until the transfer finishes or the timeout expires.
 *
 * @param dev        Device from hal_i2c_dev_add().
 * @param buf        Bytes to write; must not be NULL.
 * @param len        Number of bytes; must be non-zero.
 * @param timeout_ms Maximum time to wait, in ms.
 * @return HAL_OK, HAL_ERR_ARG, HAL_ERR_TIMEOUT, HAL_ERR_NACK or HAL_ERR_IO.
 */
hal_err_t hal_i2c_write(hal_i2c_dev_t dev, const uint8_t *buf, size_t len,
                        uint32_t timeout_ms);

/**
 * @brief Write bytes, then read bytes, with a repeated start between them.
 *
 * Blocks until the transfer finishes or the timeout expires.
 *
 * @param dev        Device from hal_i2c_dev_add().
 * @param w          Bytes to write; must not be NULL.
 * @param wlen       Number of bytes to write; must be non-zero.
 * @param r          Buffer for the bytes read; must not be NULL.
 * @param rlen       Number of bytes to read; must be non-zero.
 * @param timeout_ms Maximum time to wait, in ms.
 * @return HAL_OK, HAL_ERR_ARG, HAL_ERR_TIMEOUT, HAL_ERR_NACK or HAL_ERR_IO.
 */
hal_err_t hal_i2c_write_read(hal_i2c_dev_t dev, const uint8_t *w, size_t wlen,
                             uint8_t *r, size_t rlen, uint32_t timeout_ms);

/**
 * @brief Monotonic time since boot.
 *
 * Wraps around to 0 after about 49.7 days; compare times by unsigned
 * subtraction.
 *
 * @return Time in ms.
 */
uint32_t hal_time_ms(void);

/**
 * @brief Block the calling task for at least @p ms milliseconds.
 *
 * Task context only.
 *
 * @param ms Delay in ms.
 */
void hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
