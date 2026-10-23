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

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Longest wait node_hal_time_busy_wait_us() accepts, in µs. Longer
 *         waits must yield, so they belong in a time event. */
#define NODE_HAL_TIME_BUSY_WAIT_MAX_US 1000U

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Result of a HAL call. */
typedef enum {
  NODE_HAL_OK = 0,      /**< Success. */
  NODE_HAL_ERR_ARG,     /**< Invalid argument, or a pin that cannot be used. */
  NODE_HAL_ERR_TIMEOUT, /**< The bus operation did not finish within the
                           timeout. */
  NODE_HAL_ERR_NACK,    /**< The I2C device did not acknowledge. */
  NODE_HAL_ERR_IO,      /**< Any other hardware or driver failure. */
} node_hal_err_t;

/** @brief Direction of a GPIO pin. */
typedef enum { NODE_HAL_GPIO_IN, NODE_HAL_GPIO_OUT } node_hal_gpio_dir_t;

/** @brief Internal pull resistor of a GPIO pin. */
typedef enum {
  NODE_HAL_GPIO_PULL_NONE,
  NODE_HAL_GPIO_PULL_UP,
  NODE_HAL_GPIO_PULL_DOWN
} node_hal_gpio_pull_t;

/** @brief Edge that triggers a GPIO interrupt. */
typedef enum {
  NODE_HAL_GPIO_EDGE_RISING,
  NODE_HAL_GPIO_EDGE_FALLING,
  NODE_HAL_GPIO_EDGE_ANY
} node_hal_gpio_edge_t;

/**
 * @brief GPIO interrupt callback.
 *
 * Runs in ISR context. It must only signal a task or queue (for example with
 * xQueueSendFromISR) and return; it must not block, log or call other HAL
 * functions.
 *
 * @param pin GPIO number that triggered the interrupt.
 * @param ctx Context pointer given to node_hal_gpio_set_isr().
 */
typedef void (*node_hal_gpio_isr_cb)(int pin, void *ctx);

/** @brief Handle to an initialised I2C bus. */
typedef struct node_hal_i2c_bus *node_hal_i2c_bus_t;

/** @brief Handle to a device on an I2C bus. */
typedef struct node_hal_i2c_dev *node_hal_i2c_dev_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

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
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG for an unusable pin or invalid enum
 * value, or NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_gpio_config(int pin, node_hal_gpio_dir_t dir,
                                    node_hal_gpio_pull_t pull);

/**
 * @brief Drive an output pin.
 *
 * @param pin   GPIO number, previously configured as output.
 * @param level 0 for low, any other value for high.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG for an unusable pin, or
 * NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_gpio_write(int pin, int level);

/**
 * @brief Read the level of a pin.
 *
 * Safe to call from ISR context.
 *
 * @param pin GPIO number.
 * @return 0 or 1, or -1 if @p pin is not a valid GPIO number.
 */
int node_hal_gpio_read(int pin);

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
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG for an unusable pin, invalid edge or
 * NULL callback, or NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_gpio_set_isr(int pin, node_hal_gpio_edge_t edge,
                                     node_hal_gpio_isr_cb cb, void *ctx);

/**
 * @brief Initialise an I2C master bus.
 *
 * Call once per port during init. Bus and device handles come from fixed
 * pools, so no memory is allocated after init. The data and clock pins are
 * rejected if the hardware cannot use them, as in node_hal_gpio_config().
 *
 * @param port    I2C controller number.
 * @param sda     GPIO number of the data line.
 * @param scl     GPIO number of the clock line.
 * @param freq_hz Bus clock in Hz; must be non-zero.
 * @param out     Receives the bus handle on success; must not be NULL.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG for invalid arguments or a port already
 * in use, or NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_i2c_bus_init(int port, int sda, int scl,
                                     uint32_t freq_hz, node_hal_i2c_bus_t *out);

/**
 * @brief Register a device on a bus.
 *
 * @param bus   Bus from node_hal_i2c_bus_init().
 * @param addr7 7-bit device address (0x00–0x7F).
 * @param out   Receives the device handle on success; must not be NULL.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG for invalid arguments or a full device
 * pool, or NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_i2c_dev_add(node_hal_i2c_bus_t bus, uint8_t addr7,
                                    node_hal_i2c_dev_t *out);

/**
 * @brief Check whether a device acknowledges its address.
 *
 * Does not register a device, so it can be used to scan a bus.
 *
 * @param bus        Bus from node_hal_i2c_bus_init().
 * @param addr7      7-bit device address (0x00–0x7F).
 * @param timeout_ms Maximum time to wait, in ms.
 * @return NODE_HAL_OK if the address is acknowledged, NODE_HAL_ERR_NACK if not,
 *         NODE_HAL_ERR_ARG, NODE_HAL_ERR_TIMEOUT or NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_i2c_probe(node_hal_i2c_bus_t bus, uint8_t addr7,
                                  uint32_t timeout_ms);

/**
 * @brief Write bytes to a device in one transaction.
 *
 * Blocks until the transfer finishes or the timeout expires.
 *
 * @param dev        Device from node_hal_i2c_dev_add().
 * @param buf        Bytes to write; must not be NULL.
 * @param len        Number of bytes; must be non-zero.
 * @param timeout_ms Maximum time to wait, in ms.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG, NODE_HAL_ERR_TIMEOUT,
 * NODE_HAL_ERR_NACK if the device does not acknowledge its address, or
 * NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_i2c_write(node_hal_i2c_dev_t dev, const uint8_t *buf,
                                  size_t len, uint32_t timeout_ms);

/**
 * @brief Read bytes from a device in one transaction, with no write first.
 *
 * Blocks until the transfer finishes or the timeout expires.
 *
 * @param dev        Device from node_hal_i2c_dev_add().
 * @param buf        Buffer for the bytes read; must not be NULL.
 * @param len        Number of bytes to read; must be non-zero.
 * @param timeout_ms Maximum time to wait, in ms.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG, NODE_HAL_ERR_TIMEOUT,
 * NODE_HAL_ERR_NACK if the device does not acknowledge its address, or
 * NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_i2c_read(node_hal_i2c_dev_t dev, uint8_t *buf,
                                 size_t len, uint32_t timeout_ms);

/**
 * @brief Write bytes, then read bytes, with a repeated start between them.
 *
 * Blocks until the transfer finishes or the timeout expires.
 *
 * @param dev        Device from node_hal_i2c_dev_add().
 * @param w          Bytes to write; must not be NULL.
 * @param wlen       Number of bytes to write; must be non-zero.
 * @param r          Buffer for the bytes read; must not be NULL.
 * @param rlen       Number of bytes to read; must be non-zero.
 * @param timeout_ms Maximum time to wait, in ms.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG, NODE_HAL_ERR_TIMEOUT,
 * NODE_HAL_ERR_NACK if the device does not acknowledge its address, or
 * NODE_HAL_ERR_IO.
 */
node_hal_err_t node_hal_i2c_write_read(node_hal_i2c_dev_t dev, const uint8_t *w,
                                       size_t wlen, uint8_t *r, size_t rlen,
                                       uint32_t timeout_ms);

/**
 * @brief Monotonic time since boot.
 *
 * Wraps around to 0 after about 49.7 days; compare times by unsigned
 * subtraction.
 *
 * @return Time in ms.
 */
uint32_t node_hal_time_ms(void);

/**
 * @brief Block the calling task for at least @p ms milliseconds.
 *
 * Task context only.
 *
 * @param ms Delay in ms.
 */
void node_hal_time_delay_ms(uint32_t ms);

/**
 * @brief Spin for at least @p us microseconds without yielding the CPU.
 *
 * For the short waits a device needs between bus transfers, such as a
 * sensor's wake-up time, where node_hal_time_delay_ms() would block for a
 * whole tick. Allowed in an active object's dispatch, like a bus transfer.
 *
 * @param us Wait in µs, at most NODE_HAL_TIME_BUSY_WAIT_MAX_US.
 * @return NODE_HAL_OK, or NODE_HAL_ERR_ARG if @p us is too long; nothing is
 *         waited then.
 */
node_hal_err_t node_hal_time_busy_wait_us(uint32_t us);

#ifdef __cplusplus
}
#endif
