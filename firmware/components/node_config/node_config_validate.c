/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "node_config_internal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Highest GPIO number on the classic ESP32. */
#define NODE_CONFIG_VALIDATE_MAX_GPIO 39

/** @brief First of the input-only GPIOs 34–39, which have no output driver
 *         and no internal pull resistors. */
#define NODE_CONFIG_VALIDATE_FIRST_INPUT_ONLY_GPIO 34

/** @brief Most GPIO claims a config can make: SDA and SCL per bus, one pin
 *         per device. */
#define NODE_CONFIG_VALIDATE_MAX_CLAIMS                                        \
  (2 * NODE_CONFIG_MAX_I2C_BUSES + NODE_CONFIG_MAX_DEVICES)

/** @brief Most ids a config can hold: one per bus and one per device. */
#define NODE_CONFIG_VALIDATE_MAX_IDS                                           \
  (NODE_CONFIG_MAX_I2C_BUSES + NODE_CONFIG_MAX_DEVICES)

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief How a config entry uses a GPIO, which decides the rules that
 *         apply. */
typedef enum {
  NODE_CONFIG_VALIDATE_PIN_INPUT,           /**< Input, no pull resistor. */
  NODE_CONFIG_VALIDATE_PIN_INPUT_WITH_PULL, /**< Input with an internal pull
                                               resistor. */
  NODE_CONFIG_VALIDATE_PIN_OUTPUT,          /**< Output, including I2C open
                                               drain lines. */
} node_config_validate_pin_use_t;

/** @brief The config entry that owns an id or a GPIO, kept to name it in an
 *         error message. */
typedef struct {
  const char *list;  /**< "i2c_buses" or "devices". */
  int index;         /**< Index of the entry in @c list. */
  const char *field; /**< Key inside the entry, for example "sda". */
} node_config_validate_owner_t;

/** @brief A GPIO already claimed by an earlier entry. */
typedef struct {
  int gpio;                           /**< GPIO number. */
  node_config_validate_owner_t owner; /**< Entry that claimed it. */
} node_config_validate_claim_t;

/** @brief An id already used by an earlier entry. */
typedef struct {
  const char *id;                     /**< NUL-terminated id, pointing into
                                         the config being validated. */
  node_config_validate_owner_t owner; /**< Entry that uses it. */
} node_config_validate_id_t;

/** @brief Ids and GPIOs seen so far while walking the config in order. */
typedef struct {
  node_config_validate_claim_t claims[NODE_CONFIG_VALIDATE_MAX_CLAIMS];
  /**< GPIO claims, in config order. */
  size_t n_claims; /**< Number of valid entries in @c claims. */
  node_config_validate_id_t ids[NODE_CONFIG_VALIDATE_MAX_IDS];
  /**< Ids, in config order. */
  size_t n_ids; /**< Number of valid entries in @c ids. */
} node_config_validate_seen_t;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

static node_config_err_code_t
node_config_validate_bus(const node_config_t *cfg, int index,
                         node_config_validate_seen_t *seen,
                         node_config_err_t *err);
static node_config_err_code_t
node_config_validate_device(const node_config_dev_t *dev, int index,
                            node_config_validate_seen_t *seen,
                            node_config_err_t *err);
static node_config_err_code_t
node_config_validate_report(const node_config_report_t *report, int index,
                            node_config_err_t *err);
static node_config_err_code_t node_config_validate_use_pin(
    int gpio, node_config_validate_pin_use_t use,
    node_config_validate_owner_t owner, const char *pull_field,
    node_config_validate_seen_t *seen, node_config_err_t *err);
static node_config_err_code_t
node_config_validate_pin_rules(int gpio, node_config_validate_pin_use_t use,
                               const char *path, const char *pull_path,
                               node_config_err_t *err);
static bool node_config_validate_pin_exists(int gpio);
static bool node_config_validate_pin_is_strapping(int gpio);
static node_config_err_code_t
node_config_validate_use_id(const char *id, node_config_validate_owner_t owner,
                            node_config_validate_seen_t *seen,
                            node_config_err_t *err);
static void node_config_validate_owner_path(char *dst, size_t size,
                                            node_config_validate_owner_t owner);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/* Entries are checked in config order, so the error reported is the first
 * one in the document. */
node_config_err_code_t node_config_validate(const node_config_t *cfg,
                                            node_config_err_t *err) {
  if (cfg == NULL || cfg->n_i2c < 0 || cfg->n_i2c > NODE_CONFIG_MAX_I2C_BUSES ||
      cfg->n_dev < 0 || cfg->n_dev > NODE_CONFIG_MAX_DEVICES) {
    return node_config_err_set(err, NODE_CONFIG_ERR_ARG, NULL,
                               "invalid config to validate");
  }
  node_config_validate_seen_t seen = {0};

  for (int i = 0; i < cfg->n_i2c; i++) {
    node_config_err_code_t code = node_config_validate_bus(cfg, i, &seen, err);
    if (code != NODE_CONFIG_OK) {
      return code;
    }
  }
  for (int i = 0; i < cfg->n_dev; i++) {
    node_config_err_code_t code =
        node_config_validate_device(&cfg->dev[i], i, &seen, err);
    if (code != NODE_CONFIG_OK) {
      return code;
    }
  }
  return NODE_CONFIG_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static node_config_err_code_t
node_config_validate_bus(const node_config_t *cfg, int index,
                         node_config_validate_seen_t *seen,
                         node_config_err_t *err) {
  const node_config_i2c_bus_t *bus = &cfg->i2c[index];
  node_config_validate_owner_t owner = {"i2c_buses", index, "id"};
  node_config_err_code_t code =
      node_config_validate_use_id(bus->id, owner, seen, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  for (int i = 0; i < index; i++) {
    if (cfg->i2c[i].port == bus->port) {
      char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
      owner.field = "port";
      node_config_validate_owner_path(path, sizeof(path), owner);
      return node_config_err_set(err, NODE_CONFIG_ERR_DUPLICATE, path,
                                 "port %d is already used by i2c_buses[%d]",
                                 bus->port, i);
    }
  }

  owner.field = "sda";
  code = node_config_validate_use_pin(bus->sda, NODE_CONFIG_VALIDATE_PIN_OUTPUT,
                                      owner, NULL, seen, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }
  owner.field = "scl";
  return node_config_validate_use_pin(bus->scl, NODE_CONFIG_VALIDATE_PIN_OUTPUT,
                                      owner, NULL, seen, err);
}

static node_config_err_code_t
node_config_validate_device(const node_config_dev_t *dev, int index,
                            node_config_validate_seen_t *seen,
                            node_config_err_t *err) {
  node_config_validate_owner_t owner = {"devices", index, "id"};
  node_config_err_code_t code =
      node_config_validate_use_id(dev->id, owner, seen, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  owner.field = "gpio";
  switch (dev->type) {
  case NODE_CONFIG_DEV_SHTC3:
    return node_config_validate_report(&dev->shtc3.report, index, err);
  case NODE_CONFIG_DEV_PIR:
    return node_config_validate_use_pin(
        dev->pir.gpio,
        (dev->pir.pull == NODE_HAL_GPIO_PULL_NONE)
            ? NODE_CONFIG_VALIDATE_PIN_INPUT
            : NODE_CONFIG_VALIDATE_PIN_INPUT_WITH_PULL,
        owner, "pull", seen, err);
  case NODE_CONFIG_DEV_LED:
    return node_config_validate_use_pin(
        dev->led.gpio, NODE_CONFIG_VALIDATE_PIN_OUTPUT, owner, NULL, seen, err);
  default: {
    char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
    owner.field = "type";
    node_config_validate_owner_path(path, sizeof(path), owner);
    return node_config_err_set(err, NODE_CONFIG_ERR_DEVICE_TYPE, path,
                               "unsupported device type");
  }
  }
}

static node_config_err_code_t
node_config_validate_report(const node_config_report_t *report, int index,
                            node_config_err_t *err) {
  if (report->min_interval_ms <= report->max_interval_ms) {
    return NODE_CONFIG_OK;
  }
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_validate_owner_t owner = {"devices", index,
                                        "report.min_interval_ms"};
  node_config_validate_owner_path(path, sizeof(path), owner);
  return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, path,
                             "must not exceed max_interval_ms (%lu)",
                             (unsigned long)report->max_interval_ms);
}

/* The pin rules are checked before conflicts, so a pin that cannot be used at
 * all is reported as such even when it is also claimed twice. */
static node_config_err_code_t node_config_validate_use_pin(
    int gpio, node_config_validate_pin_use_t use,
    node_config_validate_owner_t owner, const char *pull_field,
    node_config_validate_seen_t *seen, node_config_err_t *err) {
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_validate_owner_path(path, sizeof(path), owner);
  char pull_path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  if (pull_field != NULL) {
    node_config_validate_owner_t pull_owner = owner;
    pull_owner.field = pull_field;
    node_config_validate_owner_path(pull_path, sizeof(pull_path), pull_owner);
  }

  node_config_err_code_t code =
      node_config_validate_pin_rules(gpio, use, path, pull_path, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  for (size_t i = 0; i < seen->n_claims; i++) {
    if (seen->claims[i].gpio == gpio) {
      char other[NODE_CONFIG_ERR_PATH_LEN] = {0};
      node_config_validate_owner_path(other, sizeof(other),
                                      seen->claims[i].owner);
      return node_config_err_set(err, NODE_CONFIG_ERR_DUPLICATE, path,
                                 "GPIO %d is already used by %s", gpio, other);
    }
  }
  if (seen->n_claims >= NODE_CONFIG_VALIDATE_MAX_CLAIMS) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TOO_MANY, path,
                               "too many GPIOs in use");
  }
  seen->claims[seen->n_claims].gpio = gpio;
  seen->claims[seen->n_claims].owner = owner;
  seen->n_claims++;

  if (node_config_validate_pin_is_strapping(gpio)) {
    node_config_log_warning(
        path, "GPIO %d is a strapping pin and may affect boot", gpio);
  }
  return NODE_CONFIG_OK;
}

/* Pin rules for the classic ESP32 on a WROVER module (spec 03, Hardware
 * details), from the ESP32 datasheet's pin list. */
static node_config_err_code_t
node_config_validate_pin_rules(int gpio, node_config_validate_pin_use_t use,
                               const char *path, const char *pull_path,
                               node_config_err_t *err) {
  if (!node_config_validate_pin_exists(gpio)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_PIN, path,
                               "GPIO %d does not exist on the ESP32", gpio);
  }
  if (gpio >= 6 && gpio <= 11) {
    return node_config_err_set(err, NODE_CONFIG_ERR_PIN, path,
                               "GPIO %d is used by the flash", gpio);
  }
  if (gpio == 16 || gpio == 17) {
    return node_config_err_set(err, NODE_CONFIG_ERR_PIN, path,
                               "GPIO %d is used by the PSRAM", gpio);
  }
  if (gpio == 1 || gpio == 3) {
    return node_config_err_set(err, NODE_CONFIG_ERR_PIN, path,
                               "GPIO %d is used by the serial console", gpio);
  }

  bool input_only = gpio >= NODE_CONFIG_VALIDATE_FIRST_INPUT_ONLY_GPIO;
  switch (use) {
  case NODE_CONFIG_VALIDATE_PIN_INPUT:
    return NODE_CONFIG_OK;
  case NODE_CONFIG_VALIDATE_PIN_INPUT_WITH_PULL:
    if (input_only) {
      return node_config_err_set(
          err, NODE_CONFIG_ERR_PIN,
          (pull_path != NULL && pull_path[0] != '\0') ? pull_path : path,
          "GPIO %d has no internal pull resistor", gpio);
    }
    return NODE_CONFIG_OK;
  case NODE_CONFIG_VALIDATE_PIN_OUTPUT:
    if (input_only) {
      return node_config_err_set(err, NODE_CONFIG_ERR_PIN, path,
                                 "GPIO %d is input-only", gpio);
    }
    return NODE_CONFIG_OK;
  default:
    return node_config_err_set(err, NODE_CONFIG_ERR_ARG, path,
                               "unknown pin use");
  }
}

static bool node_config_validate_pin_exists(int gpio) {
  if (gpio < 0 || gpio > NODE_CONFIG_VALIDATE_MAX_GPIO) {
    return false;
  }
  return gpio != 24 && !(gpio >= 28 && gpio <= 31);
}

static bool node_config_validate_pin_is_strapping(int gpio) {
  return gpio == 0 || gpio == 2 || gpio == 5 || gpio == 12 || gpio == 15;
}

static node_config_err_code_t
node_config_validate_use_id(const char *id, node_config_validate_owner_t owner,
                            node_config_validate_seen_t *seen,
                            node_config_err_t *err) {
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_validate_owner_path(path, sizeof(path), owner);

  for (size_t i = 0; i < seen->n_ids; i++) {
    if (strncmp(seen->ids[i].id, id, NODE_CONFIG_ID_LEN) == 0) {
      char other[NODE_CONFIG_ERR_PATH_LEN] = {0};
      node_config_validate_owner_t other_owner = seen->ids[i].owner;
      other_owner.field = NULL;
      node_config_validate_owner_path(other, sizeof(other), other_owner);
      return node_config_err_set(err, NODE_CONFIG_ERR_DUPLICATE, path,
                                 "id \"%s\" is already used by %s", id, other);
    }
  }
  if (seen->n_ids >= NODE_CONFIG_VALIDATE_MAX_IDS) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TOO_MANY, path,
                               "too many ids");
  }
  seen->ids[seen->n_ids].id = id;
  seen->ids[seen->n_ids].owner = owner;
  seen->n_ids++;
  return NODE_CONFIG_OK;
}

/* A path longer than the buffer is truncated; the error is still reported,
 * with a shortened path. */
static void
node_config_validate_owner_path(char *dst, size_t size,
                                node_config_validate_owner_t owner) {
  int written = 0;
  if (owner.field != NULL) {
    written =
        snprintf(dst, size, "%s[%d].%s", owner.list, owner.index, owner.field);
  } else {
    written = snprintf(dst, size, "%s[%d]", owner.list, owner.index);
  }
  if (written < 0 && size > 0) {
    dst[0] = '\0';
  }
}
