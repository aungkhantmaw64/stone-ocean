/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "node_hal_mock.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief I2C controllers the mock accepts, as on ESP32. */
#define I2C_PORT_COUNT 2

/** @brief Size of the device handle pool, as in the ESP implementation. */
#define I2C_MAX_DEVICES 8

/** @brief Highest 7-bit I2C address. */
#define I2C_ADDR7_MAX 0x7F

/** @brief Capacity of the failure message, including the terminating NUL. */
#define FAILURE_MESSAGE_SIZE 160

/* ==========================================================================
 * TYPES
 * ========================================================================== */

typedef enum {
  STEP_WRITE,
  STEP_READ,
  STEP_ERROR,
} node_hal_i2c_mock_step_kind_t;

typedef struct {
  node_hal_i2c_mock_step_kind_t kind;
  uint8_t addr7;
  uint8_t data[NODE_HAL_MOCK_I2C_MAX_DATA];
  size_t len;
  node_hal_err_t err;
} node_hal_i2c_mock_step_t;

struct node_hal_i2c_bus {
  bool in_use;
};

struct node_hal_i2c_dev {
  bool in_use;
  uint8_t addr7;
};

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Bus handle pool, indexed by port. */
static struct node_hal_i2c_bus g_buses[I2C_PORT_COUNT] = {0};

/** @brief Device handle pool, handed out by node_hal_i2c_dev_add(). */
static struct node_hal_i2c_dev g_devices[I2C_MAX_DEVICES] = {0};

/** @brief Addresses node_hal_i2c_probe() finds, indexed by 7-bit address. */
static bool g_present[I2C_ADDR7_MAX + 1] = {0};

/** @brief The test script: steps in the order they were queued. */
static node_hal_i2c_mock_step_t g_steps[NODE_HAL_MOCK_I2C_MAX_STEPS] = {0};

/** @brief Number of steps queued in g_steps. */
static size_t g_steps_queued = 0;

/** @brief Number of steps transfers have consumed; the next step to consume
 *         is g_steps[g_steps_consumed]. */
static size_t g_steps_consumed = 0;

/** @brief Message describing the first transfer that did not match the
 *         script; valid only while g_failed is true. */
static char g_failure[FAILURE_MESSAGE_SIZE] = {0};

/** @brief Whether a mismatching transfer has been recorded since the last
 *         reset. */
static bool g_failed = false;

/** @brief Error the next valid node_hal_i2c_bus_init() returns;
 *         NODE_HAL_OK when it should succeed. */
static node_hal_err_t g_bus_init_error = NODE_HAL_OK;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Record the first transfer that does not match the script.
 *
 * Later calls are ignored until node_hal_i2c_mock_reset().
 *
 * @param message What went wrong; must not be NULL.
 * @param addr7   Address of the transfer.
 */
static void node_hal_i2c_record_failure(const char *message, uint8_t addr7);

/**
 * @brief Append a step to the test script.
 *
 * A step that does not fit is recorded as a failure instead.
 *
 * @param kind  Kind of step.
 * @param addr7 Address the step is for.
 * @param data  Bytes to expect or return; may be NULL only if @p len is 0.
 * @param len   Number of bytes in @p data; at most NODE_HAL_MOCK_I2C_MAX_DATA.
 * @param err   Error an error step returns; NODE_HAL_OK otherwise.
 */
static void node_hal_i2c_queue_step(node_hal_i2c_mock_step_kind_t kind,
                                    uint8_t addr7, const uint8_t *data,
                                    size_t len, node_hal_err_t err);

/**
 * @brief Whether @p bus is an initialised handle from g_buses.
 *
 * @param bus Handle to check; may be NULL or any pointer.
 * @return True if @p bus points into g_buses and is in use.
 */
static bool node_hal_i2c_bus_is_valid(node_hal_i2c_bus_t bus);

/**
 * @brief Whether @p dev is an added handle from g_devices.
 *
 * @param dev Handle to check; may be NULL or any pointer.
 * @return True if @p dev points into g_devices and is in use.
 */
static bool node_hal_i2c_dev_is_valid(node_hal_i2c_dev_t dev);

/**
 * @brief Consume the next scripted step, which must be for @p addr7.
 *
 * @param addr7 Address of the transfer.
 * @return The step, or NULL after recording a failure if no step is left or
 *         it is for another address.
 */
static const node_hal_i2c_mock_step_t *node_hal_i2c_next_step(uint8_t addr7);

/**
 * @brief Match a write against the next scripted step.
 *
 * @param addr7 Address of the transfer.
 * @param buf   Bytes written; must not be NULL.
 * @param len   Number of bytes in @p buf.
 * @return NODE_HAL_OK if the step is a write of the same bytes, the step's
 *         error for an error step, or NODE_HAL_ERR_IO after recording a
 *         failure.
 */
static node_hal_err_t node_hal_i2c_match_write(uint8_t addr7,
                                               const uint8_t *buf, size_t len);

/**
 * @brief Match a read against the next scripted step and return its bytes.
 *
 * @param addr7 Address of the transfer.
 * @param r     Receives the scripted bytes; must not be NULL.
 * @param rlen  Number of bytes to read.
 * @return NODE_HAL_OK if the step is a read of @p rlen bytes, or
 *         NODE_HAL_ERR_IO after recording a failure.
 */
static node_hal_err_t node_hal_i2c_match_read(uint8_t addr7, uint8_t *r,
                                              size_t rlen);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_hal_i2c_mock_reset(void) {
  memset(g_buses, 0, sizeof g_buses);
  memset(g_devices, 0, sizeof g_devices);
  memset(g_present, 0, sizeof g_present);
  memset(g_steps, 0, sizeof g_steps);
  g_steps_queued = 0;
  g_steps_consumed = 0;
  g_failure[0] = '\0';
  g_failed = false;
  g_bus_init_error = NODE_HAL_OK;
}

void node_hal_i2c_mock_fail_next_bus_init(node_hal_err_t err) {
  g_bus_init_error = err;
}

void node_hal_i2c_mock_expect_write(uint8_t addr7, const uint8_t *data,
                                    size_t len) {
  node_hal_i2c_queue_step(STEP_WRITE, addr7, data, len, NODE_HAL_OK);
}

void node_hal_i2c_mock_queue_read(uint8_t addr7, const uint8_t *data,
                                  size_t len) {
  node_hal_i2c_queue_step(STEP_READ, addr7, data, len, NODE_HAL_OK);
}

void node_hal_i2c_mock_queue_error(uint8_t addr7, node_hal_err_t err) {
  node_hal_i2c_queue_step(STEP_ERROR, addr7, NULL, 0, err);
}

void node_hal_i2c_mock_set_present(uint8_t addr7, bool is_present) {
  if (addr7 <= I2C_ADDR7_MAX) {
    g_present[addr7] = is_present;
  }
}

const char *node_hal_i2c_mock_failure(void) {
  return g_failed ? g_failure : NULL;
}

size_t node_hal_i2c_mock_pending_steps(void) {
  return g_steps_queued - g_steps_consumed;
}

node_hal_err_t node_hal_i2c_bus_init(int port, int sda, int scl,
                                     uint32_t freq_hz,
                                     node_hal_i2c_bus_t *out) {
  if (port < 0 || port >= I2C_PORT_COUNT || g_buses[port].in_use || sda < 0 ||
      sda >= NODE_HAL_MOCK_GPIO_COUNT || scl < 0 ||
      scl >= NODE_HAL_MOCK_GPIO_COUNT || sda == scl || freq_hz == 0 ||
      out == NULL) {
    return NODE_HAL_ERR_ARG;
  }
  if (g_bus_init_error != NODE_HAL_OK) {
    const node_hal_err_t err = g_bus_init_error;
    g_bus_init_error = NODE_HAL_OK;
    return err;
  }
  g_buses[port].in_use = true;
  *out = &g_buses[port];
  return NODE_HAL_OK;
}

node_hal_err_t node_hal_i2c_dev_add(node_hal_i2c_bus_t bus, uint8_t addr7,
                                    node_hal_i2c_dev_t *out) {
  if (!node_hal_i2c_bus_is_valid(bus) || addr7 > I2C_ADDR7_MAX || out == NULL) {
    return NODE_HAL_ERR_ARG;
  }
  for (size_t i = 0; i < I2C_MAX_DEVICES; i++) {
    if (!g_devices[i].in_use) {
      g_devices[i] = (struct node_hal_i2c_dev){.in_use = true, .addr7 = addr7};
      *out = &g_devices[i];
      return NODE_HAL_OK;
    }
  }
  return NODE_HAL_ERR_ARG;
}

node_hal_err_t node_hal_i2c_probe(node_hal_i2c_bus_t bus, uint8_t addr7,
                                  uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!node_hal_i2c_bus_is_valid(bus) || addr7 > I2C_ADDR7_MAX) {
    return NODE_HAL_ERR_ARG;
  }
  return g_present[addr7] ? NODE_HAL_OK : NODE_HAL_ERR_NACK;
}

node_hal_err_t node_hal_i2c_write(node_hal_i2c_dev_t dev, const uint8_t *buf,
                                  size_t len, uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!node_hal_i2c_dev_is_valid(dev) || buf == NULL || len == 0) {
    return NODE_HAL_ERR_ARG;
  }
  return node_hal_i2c_match_write(dev->addr7, buf, len);
}

node_hal_err_t node_hal_i2c_write_read(node_hal_i2c_dev_t dev, const uint8_t *w,
                                       size_t wlen, uint8_t *r, size_t rlen,
                                       uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!node_hal_i2c_dev_is_valid(dev) || w == NULL || wlen == 0 || r == NULL ||
      rlen == 0) {
    return NODE_HAL_ERR_ARG;
  }
  node_hal_err_t err = node_hal_i2c_match_write(dev->addr7, w, wlen);
  if (err != NODE_HAL_OK) {
    return err;
  }
  return node_hal_i2c_match_read(dev->addr7, r, rlen);
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_hal_i2c_record_failure(const char *message, uint8_t addr7) {
  if (!g_failed) {
    /* snprintf always terminates the string, and a truncated message still
     * names the transfer. */
    (void)snprintf(g_failure, sizeof g_failure, "transfer to 0x%02X: %s", addr7,
                   message);
    g_failed = true;
  }
}

static void node_hal_i2c_queue_step(node_hal_i2c_mock_step_kind_t kind,
                                    uint8_t addr7, const uint8_t *data,
                                    size_t len, node_hal_err_t err) {
  if (g_steps_queued == NODE_HAL_MOCK_I2C_MAX_STEPS ||
      len > NODE_HAL_MOCK_I2C_MAX_DATA || (len > 0 && data == NULL)) {
    node_hal_i2c_record_failure(
        "test script step rejected (too many steps, "
        "too long or no data)",
        addr7);
    return;
  }
  node_hal_i2c_mock_step_t *step = &g_steps[g_steps_queued++];
  *step = (node_hal_i2c_mock_step_t){
      .kind = kind, .addr7 = addr7, .len = len, .err = err};
  if (len > 0) {
    memcpy(step->data, data, len);
  }
}

static bool node_hal_i2c_bus_is_valid(node_hal_i2c_bus_t bus) {
  for (size_t i = 0; i < I2C_PORT_COUNT; i++) {
    if (bus == &g_buses[i]) {
      return bus->in_use;
    }
  }
  return false;
}

static bool node_hal_i2c_dev_is_valid(node_hal_i2c_dev_t dev) {
  for (size_t i = 0; i < I2C_MAX_DEVICES; i++) {
    if (dev == &g_devices[i]) {
      return dev->in_use;
    }
  }
  return false;
}

static const node_hal_i2c_mock_step_t *node_hal_i2c_next_step(uint8_t addr7) {
  if (g_steps_consumed == g_steps_queued) {
    node_hal_i2c_record_failure("no scripted step left", addr7);
    return NULL;
  }
  const node_hal_i2c_mock_step_t *step = &g_steps[g_steps_consumed++];
  if (step->addr7 != addr7) {
    node_hal_i2c_record_failure("next scripted step is for another address",
                                addr7);
    return NULL;
  }
  return step;
}

/* Consumes the write step, or an error step in its place. Returns
 * NODE_HAL_OK when the bytes match. */
static node_hal_err_t node_hal_i2c_match_write(uint8_t addr7,
                                               const uint8_t *buf, size_t len) {
  const node_hal_i2c_mock_step_t *step = node_hal_i2c_next_step(addr7);
  if (step == NULL) {
    return NODE_HAL_ERR_IO;
  }
  if (step->kind == STEP_ERROR) {
    return step->err;
  }
  if (step->kind != STEP_WRITE) {
    node_hal_i2c_record_failure("expected a read, got a write", addr7);
    return NODE_HAL_ERR_IO;
  }
  if (step->len != len || memcmp(step->data, buf, len) != 0) {
    node_hal_i2c_record_failure("written bytes differ from the script", addr7);
    return NODE_HAL_ERR_IO;
  }
  return NODE_HAL_OK;
}

static node_hal_err_t node_hal_i2c_match_read(uint8_t addr7, uint8_t *r,
                                              size_t rlen) {
  const node_hal_i2c_mock_step_t *step = node_hal_i2c_next_step(addr7);
  if (step == NULL) {
    return NODE_HAL_ERR_IO;
  }
  if (step->kind != STEP_READ) {
    node_hal_i2c_record_failure("expected a write, got a read", addr7);
    return NODE_HAL_ERR_IO;
  }
  if (step->len != rlen) {
    node_hal_i2c_record_failure("read length differs from the script", addr7);
    return NODE_HAL_ERR_IO;
  }
  memcpy(r, step->data, rlen);
  return NODE_HAL_OK;
}
