#include <limits.h>
#include <stdbool.h>

#include "driver/i2c_master.h"

#include "node_hal.h"
#include "node_hal_err.h"
#include "node_hal_gpio_internal.h"

#define I2C_MAX_DEVICES 8
#define I2C_ADDR7_MAX 0x7F
#define I2C_GLITCH_IGNORE_CYCLES 7

struct node_hal_i2c_bus {
  bool in_use;
  uint32_t freq_hz;
  i2c_master_bus_handle_t handle;
};

struct node_hal_i2c_dev {
  bool in_use;
  uint8_t addr7;
  struct node_hal_i2c_bus *bus;
  i2c_master_dev_handle_t handle;
};

static struct node_hal_i2c_bus buses[SOC_I2C_NUM];
static struct node_hal_i2c_dev devices[I2C_MAX_DEVICES];

static bool node_hal_i2c_bus_is_valid(node_hal_i2c_bus_t bus) {
  for (size_t i = 0; i < SOC_I2C_NUM; i++) {
    if (bus == &buses[i]) {
      return bus->in_use;
    }
  }
  return false;
}

static bool node_hal_i2c_dev_is_valid(node_hal_i2c_dev_t dev) {
  for (size_t i = 0; i < I2C_MAX_DEVICES; i++) {
    if (dev == &devices[i]) {
      return dev->in_use;
    }
  }
  return false;
}

static int node_hal_i2c_timeout_for_idf(uint32_t timeout_ms) {
  return timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
}

node_hal_err_t node_hal_i2c_bus_init(int port, int sda, int scl,
                                     uint32_t freq_hz,
                                     node_hal_i2c_bus_t *out) {
  if (port < 0 || port >= SOC_I2C_NUM || buses[port].in_use ||
      !node_hal_gpio_pin_is_usable(sda) || !node_hal_gpio_pin_is_usable(scl) ||
      sda == scl || freq_hz == 0 || out == NULL) {
    return NODE_HAL_ERR_ARG;
  }

  const i2c_master_bus_config_t cfg = {
      .i2c_port = port,
      .sda_io_num = sda,
      .scl_io_num = scl,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = I2C_GLITCH_IGNORE_CYCLES,
      .flags.enable_internal_pullup = true,
  };
  struct node_hal_i2c_bus *bus = &buses[port];
  node_hal_err_t err =
      node_hal_err_from_esp(i2c_new_master_bus(&cfg, &bus->handle));
  if (err != NODE_HAL_OK) {
    return err;
  }
  bus->freq_hz = freq_hz;
  bus->in_use = true;
  *out = bus;
  return NODE_HAL_OK;
}

static struct node_hal_i2c_dev *node_hal_i2c_free_device_slot(void) {
  for (size_t i = 0; i < I2C_MAX_DEVICES; i++) {
    if (!devices[i].in_use) {
      return &devices[i];
    }
  }
  return NULL;
}

node_hal_err_t node_hal_i2c_dev_add(node_hal_i2c_bus_t bus, uint8_t addr7,
                                    node_hal_i2c_dev_t *out) {
  struct node_hal_i2c_dev *dev = node_hal_i2c_free_device_slot();
  if (!node_hal_i2c_bus_is_valid(bus) || addr7 > I2C_ADDR7_MAX || out == NULL ||
      dev == NULL) {
    return NODE_HAL_ERR_ARG;
  }

  const i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr7,
      .scl_speed_hz = bus->freq_hz,
  };
  node_hal_err_t err = node_hal_err_from_esp(
      i2c_master_bus_add_device(bus->handle, &cfg, &dev->handle));
  if (err != NODE_HAL_OK) {
    return err;
  }
  dev->addr7 = addr7;
  dev->bus = bus;
  dev->in_use = true;
  *out = dev;
  return NODE_HAL_OK;
}

static node_hal_err_t node_hal_i2c_probe_result(esp_err_t err) {
  return err == ESP_ERR_NOT_FOUND ? NODE_HAL_ERR_NACK
                                  : node_hal_err_from_esp(err);
}

node_hal_err_t node_hal_i2c_probe(node_hal_i2c_bus_t bus, uint8_t addr7,
                                  uint32_t timeout_ms) {
  if (!node_hal_i2c_bus_is_valid(bus) || addr7 > I2C_ADDR7_MAX) {
    return NODE_HAL_ERR_ARG;
  }
  return node_hal_i2c_probe_result(i2c_master_probe(
      bus->handle, addr7, node_hal_i2c_timeout_for_idf(timeout_ms)));
}

/* In IDF 5.4.4 a synchronous transfer reports both a NACK and a hardware bus
 * timeout as ESP_ERR_INVALID_STATE (i2c_master.c, s_i2c_transaction_start).
 * Probing the address afterwards tells them apart: if the address is now
 * acknowledged, the failure was in the data phase and is reported as IO. */
static node_hal_err_t node_hal_i2c_transfer_result(node_hal_i2c_dev_t dev,
                                                   esp_err_t err,
                                                   uint32_t timeout_ms) {
  if (err != ESP_ERR_INVALID_STATE) {
    return node_hal_err_from_esp(err);
  }
  node_hal_err_t probed = node_hal_i2c_probe(dev->bus, dev->addr7, timeout_ms);
  return probed == NODE_HAL_OK ? NODE_HAL_ERR_IO : probed;
}

node_hal_err_t node_hal_i2c_write(node_hal_i2c_dev_t dev, const uint8_t *buf,
                                  size_t len, uint32_t timeout_ms) {
  if (!node_hal_i2c_dev_is_valid(dev) || buf == NULL || len == 0) {
    return NODE_HAL_ERR_ARG;
  }
  esp_err_t err = i2c_master_transmit(dev->handle, buf, len,
                                      node_hal_i2c_timeout_for_idf(timeout_ms));
  return node_hal_i2c_transfer_result(dev, err, timeout_ms);
}

node_hal_err_t node_hal_i2c_write_read(node_hal_i2c_dev_t dev, const uint8_t *w,
                                       size_t wlen, uint8_t *r, size_t rlen,
                                       uint32_t timeout_ms) {
  if (!node_hal_i2c_dev_is_valid(dev) || w == NULL || wlen == 0 || r == NULL ||
      rlen == 0) {
    return NODE_HAL_ERR_ARG;
  }
  esp_err_t err = i2c_master_transmit_receive(
      dev->handle, w, wlen, r, rlen, node_hal_i2c_timeout_for_idf(timeout_ms));
  return node_hal_i2c_transfer_result(dev, err, timeout_ms);
}
