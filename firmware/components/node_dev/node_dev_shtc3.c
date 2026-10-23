/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev_shtc3.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_config.h"
#include "node_dev.h"
#include "node_dev_log.h"
#include "node_hal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Fixed 7-bit I2C address of the SHTC3 (datasheet). */
#define NODE_DEV_SHTC3_I2C_ADDR 0x70U

/** @brief Timeout of every I2C transfer to the sensor, in ms; far above the
 *         ~0.7 ms of the longest transfer at 100 kHz. */
#define NODE_DEV_SHTC3_I2C_TIMEOUT_MS 10U

/** @brief Wakeup command (datasheet). */
#define NODE_DEV_SHTC3_CMD_WAKEUP 0x3517U

/** @brief Sleep command (datasheet). */
#define NODE_DEV_SHTC3_CMD_SLEEP 0xB098U

/** @brief Soft reset command (datasheet). */
#define NODE_DEV_SHTC3_CMD_SOFT_RESET 0x805DU

/** @brief Read ID register command (datasheet). */
#define NODE_DEV_SHTC3_CMD_READ_ID 0xEFC8U

/** @brief Measure command: normal mode, temperature first, clock stretching
 *         disabled (datasheet). */
#define NODE_DEV_SHTC3_CMD_MEASURE 0x7866U

/** @brief Time from the measure command until the result is read, in ms
 *         (datasheet: 12.1 ms max in normal mode, rounded up). */
#define NODE_DEV_SHTC3_MEASURE_TIME_MS 13U

/** @brief Data words of a measurement: temperature, then humidity. */
#define NODE_DEV_SHTC3_MEASURE_WORDS 2U

/** @brief Position of the temperature word in a measurement. */
#define NODE_DEV_SHTC3_TEMP_WORD 0U

/** @brief Position of the humidity word in a measurement. */
#define NODE_DEV_SHTC3_RH_WORD 1U

/** @brief Longest time the sensor needs after a wakeup command before it
 *         accepts another, in µs (datasheet: 240 µs max). */
#define NODE_DEV_SHTC3_WAKEUP_TIME_US 240U

/** @brief Longest time the sensor needs after a soft reset, in µs
 *         (datasheet: 240 µs max). */
#define NODE_DEV_SHTC3_SOFT_RESET_TIME_US 240U

/** @brief Bits of the ID register that hold the product code (bits 11 and
 *         5:0, datasheet). */
#define NODE_DEV_SHTC3_ID_PRODUCT_MASK 0x083FU

/** @brief Product code bits of an SHTC3's ID register (datasheet). */
#define NODE_DEV_SHTC3_ID_PRODUCT_CODE 0x0807U

/** @brief Bytes of one data word on the bus: MSB, LSB, then CRC-8. */
#define NODE_DEV_SHTC3_WORD_SIZE 3U

/** @brief Data bytes of a word on the bus, before its CRC. */
#define NODE_DEV_SHTC3_WORD_DATA_SIZE 2U

/** @brief Position of the CRC byte in a word on the bus. */
#define NODE_DEV_SHTC3_WORD_CRC_INDEX 2U

/** @brief Bytes of a command on the bus. */
#define NODE_DEV_SHTC3_CMD_SIZE 2U

/** @brief Most data words read in one transfer: temperature and humidity. */
#define NODE_DEV_SHTC3_MAX_WORDS 2U

/** @brief CRC-8 generator polynomial x^8 + x^5 + x^4 + 1 (datasheet). */
#define NODE_DEV_SHTC3_CRC_POLY 0x31U

/** @brief CRC-8 initial value (datasheet). */
#define NODE_DEV_SHTC3_CRC_INIT 0xFFU

/** @brief Most significant bit of a CRC byte. */
#define NODE_DEV_SHTC3_CRC_MSB 0x80U

/** @brief Bits in a byte. */
#define NODE_DEV_SHTC3_BITS_PER_BYTE 8U

/** @brief Full scale of a raw 16-bit word, 2^16, in the conversion
 *         formulas. */
#define NODE_DEV_SHTC3_RAW_FULL_SCALE 65536.0f

/** @brief Temperature offset of the conversion formula, in °C. */
#define NODE_DEV_SHTC3_TEMP_OFFSET_C (-45.0f)

/** @brief Temperature span of the conversion formula, in °C. */
#define NODE_DEV_SHTC3_TEMP_SPAN_C 175.0f

/** @brief Humidity span of the conversion formula, in %. */
#define NODE_DEV_SHTC3_RH_SPAN_PCT 100.0f

/** @brief Lowest humidity reported, in %. */
#define NODE_DEV_SHTC3_RH_MIN_PCT 0.0f

/** @brief Highest humidity reported, in %. */
#define NODE_DEV_SHTC3_RH_MAX_PCT 100.0f

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Driver state of one SHTC3 device; written only in the devices AO. */
typedef struct {
  node_hal_i2c_bus_t bus; /**< Bus @c dev was added on; NULL until the first
                             init. */
  node_hal_i2c_dev_t dev; /**< The sensor on @c bus; added once and reused by
                             init retries, as the HAL cannot remove a
                             device. */
  bool measuring;         /**< True between starting a measurement and
                             reading its result. */
} node_dev_shtc3_state_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Driver state of every SHTC3 device, by device index; written by the
 *         SHTC3 driver's methods in the devices AO. */
static node_dev_shtc3_state_t g_shtc3_states[NODE_CONFIG_MAX_DEVICES] = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Add the sensor to its bus, reset it, check and log its ID, and put
 *        it to sleep.
 *
 * @param args  The device's index, config and bus; its type must be
 *              NODE_CONFIG_DEV_SHTC3 and its bus non-NULL.
 * @param state Receives the device's node_dev_shtc3_state_t.
 * @param sched Receives the config's sample interval and report policy.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for bad arguments, or
 *         NODE_DEV_ERR_IO if the sensor did not answer, a CRC was wrong or
 *         the ID is not an SHTC3's.
 */
static node_dev_err_t node_dev_shtc3_init(const node_dev_init_args_t *args,
                                          void **state,
                                          node_dev_sched_t *sched);

/**
 * @brief Take one measurement in two calls: the first starts it and returns
 *        NODE_DEV_PENDING, the next reads the result and puts the sensor to
 *        sleep.
 *
 * @param state    The device's node_dev_shtc3_state_t.
 * @param out      Receives @c temp_c and @c rh_pct; set only on NODE_DEV_OK.
 * @param again_ms Receives the time until the result can be read, with
 *                 NODE_DEV_PENDING.
 * @return NODE_DEV_PENDING after starting, NODE_DEV_OK with the reading,
 *         NODE_DEV_ERR_ARG for NULL pointers, or NODE_DEV_ERR_IO if a
 *         transfer failed or a CRC was wrong; the next call then starts a new
 *         measurement.
 */
static node_dev_err_t node_dev_shtc3_sample(void *state, node_dev_value_t *out,
                                            uint32_t *again_ms);

/**
 * @brief Wake the sensor and start a measurement.
 *
 * @param dev The sensor.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if a transfer failed; the sensor
 *         is then sent back to sleep if it can be.
 */
static node_dev_err_t node_dev_shtc3_start_measurement(node_hal_i2c_dev_t dev);

/**
 * @brief Read and convert a finished measurement, then put the sensor to
 *        sleep.
 *
 * The sensor is sent to sleep even if the read fails.
 *
 * @param dev The sensor.
 * @param out Receives @c temp_c and @c rh_pct; set only on NODE_DEV_OK.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if the read failed, a CRC was
 *         wrong or the sensor could not be put to sleep.
 */
static node_dev_err_t node_dev_shtc3_read_measurement(node_hal_i2c_dev_t dev,
                                                      node_dev_value_t *out);

/**
 * @brief Add the sensor to @p bus, unless it was already added there.
 *
 * @param shtc3 The device's state; @c bus and @c dev are updated.
 * @param bus   The device's I2C bus; must not be NULL.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if the HAL could not add it.
 */
static node_dev_err_t node_dev_shtc3_attach(node_dev_shtc3_state_t *shtc3,
                                            node_hal_i2c_bus_t bus);

/**
 * @brief Send a 16-bit command, MSB first.
 *
 * @param dev The sensor.
 * @param cmd One of NODE_DEV_SHTC3_CMD_*.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if the transfer failed.
 */
static node_dev_err_t node_dev_shtc3_send(node_hal_i2c_dev_t dev, uint16_t cmd);

/**
 * @brief Send a command, then busy-wait the time the sensor needs after it.
 *
 * @param dev     The sensor.
 * @param cmd     One of NODE_DEV_SHTC3_CMD_*.
 * @param wait_us Time to wait after the command, in µs; at most
 *                NODE_HAL_TIME_BUSY_WAIT_MAX_US.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if the transfer or the wait failed.
 */
static node_dev_err_t node_dev_shtc3_send_and_wait(node_hal_i2c_dev_t dev,
                                                   uint16_t cmd,
                                                   uint32_t wait_us);

/**
 * @brief Read data words, each MSB first followed by its CRC-8, and check
 *        every CRC.
 *
 * @param dev     The sensor.
 * @param words   Receives the words; untouched unless every CRC is right.
 * @param n_words Number of words, 1 to NODE_DEV_SHTC3_MAX_WORDS.
 * @return NODE_DEV_OK, NODE_DEV_ERR_ARG for a bad count, or NODE_DEV_ERR_IO
 *         if the transfer failed or a CRC was wrong.
 */
static node_dev_err_t node_dev_shtc3_read_words(node_hal_i2c_dev_t dev,
                                                uint16_t *words,
                                                size_t n_words);

/**
 * @brief Read the ID register and check that it is an SHTC3's.
 *
 * The sensor must be awake.
 *
 * @param dev The sensor.
 * @param id  Receives the whole ID register.
 * @return NODE_DEV_OK, or NODE_DEV_ERR_IO if the read failed, its CRC was
 *         wrong or the product code is not an SHTC3's.
 */
static node_dev_err_t node_dev_shtc3_read_id(node_hal_i2c_dev_t dev,
                                             uint16_t *id);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_dev_err_t node_dev_shtc3_register(void) {
  /* Function scope, so the table can follow the prototypes it points to;
   * static, because the framework keeps the pointer forever. */
  static const node_dev_driver_t driver = {
      .type = NODE_CONFIG_DEV_SHTC3,
      .sample_sig = NODE_DEV_SIG_READING,
      .init = node_dev_shtc3_init,
      .sample = node_dev_shtc3_sample,
      .set_onoff = NULL,
      .on_isr = NULL,
  };
  return node_dev_register_driver(&driver);
}

uint8_t node_dev_shtc3_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = NODE_DEV_SHTC3_CRC_INIT;
  if (data == NULL) {
    return crc;
  }
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < NODE_DEV_SHTC3_BITS_PER_BYTE; bit++) {
      if ((crc & NODE_DEV_SHTC3_CRC_MSB) != 0U) {
        crc = (uint8_t)((uint8_t)(crc << 1) ^ NODE_DEV_SHTC3_CRC_POLY);
      } else {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return crc;
}

float node_dev_shtc3_raw_to_temp_c(uint16_t raw) {
  return NODE_DEV_SHTC3_TEMP_OFFSET_C +
         (NODE_DEV_SHTC3_TEMP_SPAN_C * (float)raw /
          NODE_DEV_SHTC3_RAW_FULL_SCALE);
}

float node_dev_shtc3_raw_to_rh_pct(uint16_t raw) {
  const float rh_pct =
      NODE_DEV_SHTC3_RH_SPAN_PCT * (float)raw / NODE_DEV_SHTC3_RAW_FULL_SCALE;
  if (rh_pct < NODE_DEV_SHTC3_RH_MIN_PCT) {
    return NODE_DEV_SHTC3_RH_MIN_PCT;
  }
  if (rh_pct > NODE_DEV_SHTC3_RH_MAX_PCT) {
    return NODE_DEV_SHTC3_RH_MAX_PCT;
  }
  return rh_pct;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_dev_err_t node_dev_shtc3_init(const node_dev_init_args_t *args,
                                          void **state,
                                          node_dev_sched_t *sched) {
  if ((args == NULL) || (args->cfg == NULL) || (args->bus == NULL) ||
      (state == NULL) || (sched == NULL) || (args->dev_index < 0) ||
      (args->dev_index >= NODE_CONFIG_MAX_DEVICES) ||
      (args->cfg->type != NODE_CONFIG_DEV_SHTC3)) {
    return NODE_DEV_ERR_ARG;
  }
  node_dev_shtc3_state_t *shtc3 = &g_shtc3_states[args->dev_index];
  shtc3->measuring = false;
  node_dev_err_t err = node_dev_shtc3_attach(shtc3, args->bus);
  if (err != NODE_DEV_OK) {
    return err;
  }

  /* A sensor left asleep by an earlier run accepts only the wakeup command,
   * so wake it before resetting it. */
  err = node_dev_shtc3_send_and_wait(shtc3->dev, NODE_DEV_SHTC3_CMD_WAKEUP,
                                     NODE_DEV_SHTC3_WAKEUP_TIME_US);
  if (err != NODE_DEV_OK) {
    return err;
  }
  err = node_dev_shtc3_send_and_wait(shtc3->dev, NODE_DEV_SHTC3_CMD_SOFT_RESET,
                                     NODE_DEV_SHTC3_SOFT_RESET_TIME_US);
  if (err != NODE_DEV_OK) {
    return err;
  }
  uint16_t id = 0;
  err = node_dev_shtc3_read_id(shtc3->dev, &id);
  if (err != NODE_DEV_OK) {
    return err;
  }
  node_dev_log_info("[%d] '%s': SHTC3 id 0x%04X", args->dev_index,
                    args->cfg->id, (unsigned)id);
  err = node_dev_shtc3_send(shtc3->dev, NODE_DEV_SHTC3_CMD_SLEEP);
  if (err != NODE_DEV_OK) {
    return err;
  }

  sched->sample_interval_ms = args->cfg->shtc3.sample_interval_ms;
  sched->report = &args->cfg->shtc3.report;
  *state = shtc3;
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_sample(void *state, node_dev_value_t *out,
                                            uint32_t *again_ms) {
  if ((state == NULL) || (out == NULL) || (again_ms == NULL)) {
    return NODE_DEV_ERR_ARG;
  }
  node_dev_shtc3_state_t *shtc3 = state;
  if (!shtc3->measuring) {
    const node_dev_err_t err = node_dev_shtc3_start_measurement(shtc3->dev);
    if (err != NODE_DEV_OK) {
      return err;
    }
    shtc3->measuring = true;
    *again_ms = NODE_DEV_SHTC3_MEASURE_TIME_MS;
    return NODE_DEV_PENDING;
  }
  shtc3->measuring = false;
  return node_dev_shtc3_read_measurement(shtc3->dev, out);
}

static node_dev_err_t node_dev_shtc3_start_measurement(node_hal_i2c_dev_t dev) {
  node_dev_err_t err = node_dev_shtc3_send_and_wait(
      dev, NODE_DEV_SHTC3_CMD_WAKEUP, NODE_DEV_SHTC3_WAKEUP_TIME_US);
  if (err != NODE_DEV_OK) {
    return err;
  }
  err = node_dev_shtc3_send(dev, NODE_DEV_SHTC3_CMD_MEASURE);
  if (err != NODE_DEV_OK) {
    /* Best effort: the measure command already failed, and that is the
     * error to report. */
    (void)node_dev_shtc3_send(dev, NODE_DEV_SHTC3_CMD_SLEEP);
    return err;
  }
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_read_measurement(node_hal_i2c_dev_t dev,
                                                      node_dev_value_t *out) {
  uint16_t words[NODE_DEV_SHTC3_MEASURE_WORDS] = {0};
  const node_dev_err_t read_err =
      node_dev_shtc3_read_words(dev, words, NODE_DEV_SHTC3_MEASURE_WORDS);
  const node_dev_err_t sleep_err =
      node_dev_shtc3_send(dev, NODE_DEV_SHTC3_CMD_SLEEP);
  if (read_err != NODE_DEV_OK) {
    return read_err;
  }
  if (sleep_err != NODE_DEV_OK) {
    return sleep_err;
  }
  out->temp_c = node_dev_shtc3_raw_to_temp_c(words[NODE_DEV_SHTC3_TEMP_WORD]);
  out->rh_pct = node_dev_shtc3_raw_to_rh_pct(words[NODE_DEV_SHTC3_RH_WORD]);
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_attach(node_dev_shtc3_state_t *shtc3,
                                            node_hal_i2c_bus_t bus) {
  if ((shtc3->dev != NULL) && (shtc3->bus == bus)) {
    return NODE_DEV_OK;
  }
  node_hal_i2c_dev_t dev = NULL;
  if (node_hal_i2c_dev_add(bus, NODE_DEV_SHTC3_I2C_ADDR, &dev) != NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  shtc3->bus = bus;
  shtc3->dev = dev;
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_send(node_hal_i2c_dev_t dev,
                                          uint16_t cmd) {
  const uint8_t bytes[NODE_DEV_SHTC3_CMD_SIZE] = {(uint8_t)(cmd >> 8),
                                                  (uint8_t)(cmd & 0xFFU)};
  if (node_hal_i2c_write(dev, bytes, sizeof(bytes),
                         NODE_DEV_SHTC3_I2C_TIMEOUT_MS) != NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_send_and_wait(node_hal_i2c_dev_t dev,
                                                   uint16_t cmd,
                                                   uint32_t wait_us) {
  const node_dev_err_t err = node_dev_shtc3_send(dev, cmd);
  if (err != NODE_DEV_OK) {
    return err;
  }
  if (node_hal_time_busy_wait_us(wait_us) != NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_read_words(node_hal_i2c_dev_t dev,
                                                uint16_t *words,
                                                size_t n_words) {
  if ((words == NULL) || (n_words == 0U) ||
      (n_words > NODE_DEV_SHTC3_MAX_WORDS)) {
    return NODE_DEV_ERR_ARG;
  }
  uint8_t bytes[NODE_DEV_SHTC3_MAX_WORDS * NODE_DEV_SHTC3_WORD_SIZE] = {0};
  const size_t len = n_words * NODE_DEV_SHTC3_WORD_SIZE;
  if (node_hal_i2c_read(dev, bytes, len, NODE_DEV_SHTC3_I2C_TIMEOUT_MS) !=
      NODE_HAL_OK) {
    return NODE_DEV_ERR_IO;
  }
  for (size_t i = 0; i < n_words; i++) {
    const uint8_t *word = &bytes[i * NODE_DEV_SHTC3_WORD_SIZE];
    if (node_dev_shtc3_crc8(word, NODE_DEV_SHTC3_WORD_DATA_SIZE) !=
        word[NODE_DEV_SHTC3_WORD_CRC_INDEX]) {
      return NODE_DEV_ERR_IO;
    }
  }
  for (size_t i = 0; i < n_words; i++) {
    const uint8_t *word = &bytes[i * NODE_DEV_SHTC3_WORD_SIZE];
    words[i] = (uint16_t)(((uint16_t)word[0] << 8) | word[1]);
  }
  return NODE_DEV_OK;
}

static node_dev_err_t node_dev_shtc3_read_id(node_hal_i2c_dev_t dev,
                                             uint16_t *id) {
  node_dev_err_t err = node_dev_shtc3_send(dev, NODE_DEV_SHTC3_CMD_READ_ID);
  if (err != NODE_DEV_OK) {
    return err;
  }
  uint16_t word = 0;
  err = node_dev_shtc3_read_words(dev, &word, 1U);
  if (err != NODE_DEV_OK) {
    return err;
  }
  if ((word & NODE_DEV_SHTC3_ID_PRODUCT_MASK) !=
      NODE_DEV_SHTC3_ID_PRODUCT_CODE) {
    return NODE_DEV_ERR_IO;
  }
  *id = word;
  return NODE_DEV_OK;
}
