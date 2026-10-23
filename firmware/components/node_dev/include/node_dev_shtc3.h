#pragma once

/**
 * @file node_dev_shtc3.h
 * @brief Sensirion SHTC3 temperature/humidity driver for the devices AO.
 *
 * An SHTC3 device is a sensor on an I2C bus, sampled every
 * sample_interval_ms; its readings are published as NODE_DEV_SIG_READING,
 * throttled by the device's report policy.
 *
 * The pure helpers below implement the datasheet's CRC and conversion
 * formulas; they are exposed so host tests can check them directly.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stddef.h>
#include <stdint.h>

#include "node_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Register the SHTC3 driver for NODE_CONFIG_DEV_SHTC3 devices.
 *
 * Task context, before node_dev_start(). Init resets the sensor, checks and
 * logs its ID and puts it to sleep; a sensor that does not answer or is not
 * an SHTC3 fails init, and the framework retries it. Init waits for the
 * sensor's wake-up and reset times with node_hal_time_delay_ms(). Each sample
 * wakes the sensor, starts a measurement 1 ms later, then reads it 13 ms
 * later and puts the sensor back to sleep, without blocking; a bus error or
 * bad CRC fails the sample and reports no reading.
 *
 * @return As for node_dev_register_driver().
 */
node_dev_err_t node_dev_shtc3_register(void);

/**
 * @brief CRC-8 of SHTC3 data: polynomial 0x31, initial value 0xFF, no final
 *        XOR.
 *
 * Any context.
 *
 * @param data Bytes to check, normally one 16-bit word MSB first; may be NULL
 *             only if @p len is 0.
 * @param len  Number of bytes.
 * @return The CRC, or the initial value 0xFF if @p data is NULL or @p len is
 *         0.
 */
uint8_t node_dev_shtc3_crc8(const uint8_t *data, size_t len);

/**
 * @brief Convert a raw temperature word to degrees Celsius.
 *
 * Any context. Uses the datasheet formula T = -45 + 175 * raw / 65536.
 *
 * @param raw Raw temperature word from a measurement.
 * @return Temperature in °C, from -45 to just under 130.
 */
float node_dev_shtc3_raw_to_temp_c(uint16_t raw);

/**
 * @brief Convert a raw humidity word to percent relative humidity.
 *
 * Any context. Uses the datasheet formula RH = 100 * raw / 65536, clamped to
 * 0–100 %.
 *
 * @param raw Raw humidity word from a measurement.
 * @return Relative humidity in %, from 0 to 100.
 */
float node_dev_shtc3_raw_to_rh_pct(uint16_t raw);

#ifdef __cplusplus
}
#endif
