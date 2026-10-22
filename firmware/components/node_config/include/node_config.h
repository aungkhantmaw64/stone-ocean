#pragma once

/**
 * @file node_config.h
 * @brief JSON node config: parse and validate raw JSON into node_config_t.
 *
 * The parser is pure logic and transport-agnostic: the JSON may come from the
 * serial CLI, flash storage or, later, the mesh. It builds on the target and
 * on the host. The header must not include ESP-IDF headers.
 *
 * Functions are called from task context and are not safe to call from an
 * ISR.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Maximum number of entries in "devices". */
#define NODE_CONFIG_MAX_DEVICES 8

/** @brief Maximum number of entries in "i2c_buses". */
#define NODE_CONFIG_MAX_I2C_BUSES 2

/** @brief Size of an id buffer, including the terminating NUL. */
#define NODE_CONFIG_ID_LEN 16

/** @brief Size of the node name buffer, including the terminating NUL. */
#define NODE_CONFIG_NAME_LEN 32

/** @brief Longest raw JSON config accepted, in bytes. */
#define NODE_CONFIG_MAX_RAW_LEN 4096

/** @brief Size of node_config_err_t::path, including the terminating NUL. */
#define NODE_CONFIG_ERR_PATH_LEN 48

/** @brief Size of node_config_err_t::msg, including the terminating NUL. */
#define NODE_CONFIG_ERR_MSG_LEN 80

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Kind of device attached to the node. */
typedef enum {
  NODE_CONFIG_DEV_SHTC3, /**< SHTC3 temperature/humidity sensor on I2C. */
  NODE_CONFIG_DEV_PIR,   /**< PIR motion sensor on a GPIO input. */
  NODE_CONFIG_DEV_LED,   /**< LED on a GPIO output. */
} node_config_dev_type_t;

/** @brief One I2C master bus. */
typedef struct {
  char id[NODE_CONFIG_ID_LEN]; /**< Unique, non-empty, NUL-terminated id. */
  int port;                    /**< I2C controller number, 0 or 1. */
  int sda;                     /**< GPIO number of the data line. */
  int scl;                     /**< GPIO number of the clock line. */
  uint32_t freq_hz;            /**< Bus clock in Hz, 10000 to 400000. */
} node_config_i2c_bus_t;

/** @brief When a sensor sends a reading to the mesh. */
typedef struct {
  uint32_t min_interval_ms; /**< Shortest time between reports, in ms; at
                               most max_interval_ms. */
  uint32_t max_interval_ms; /**< Longest time between reports, in ms. */
  float delta_temp_c;       /**< Temperature change that triggers a report,
                               in °C. */
  float delta_rh_pct;       /**< Humidity change that triggers a report, in
                               percent relative humidity. */
} node_config_report_t;

/** @brief SHTC3 device settings. */
typedef struct {
  int bus_index;               /**< Index into node_config_t::i2c. */
  uint32_t sample_interval_ms; /**< Time between measurements, in ms; at least
                                  1000. */
  node_config_report_t report; /**< Reporting policy. */
} node_config_shtc3_t;

/** @brief PIR device settings. */
typedef struct {
  int gpio;                  /**< GPIO number of the sensor output. */
  bool active_high;          /**< True if a high level means motion. */
  uint32_t debounce_ms;      /**< Time the level must be stable, in ms. */
  node_hal_gpio_pull_t pull; /**< Internal pull resistor to enable. */
} node_config_pir_t;

/** @brief LED device settings. */
typedef struct {
  int gpio;         /**< GPIO number that drives the LED. */
  bool active_high; /**< True if a high level turns the LED on. */
  bool default_on;  /**< State of the LED at boot. */
} node_config_led_t;

/** @brief One device entry. Only the union member matching @c type is set. */
typedef struct {
  char id[NODE_CONFIG_ID_LEN]; /**< Unique, non-empty, NUL-terminated id. */
  node_config_dev_type_t type; /**< Kind of device; selects the union member. */
  union {
    node_config_shtc3_t shtc3; /**< Set when type is NODE_CONFIG_DEV_SHTC3. */
    node_config_pir_t pir;     /**< Set when type is NODE_CONFIG_DEV_PIR. */
    node_config_led_t led;     /**< Set when type is NODE_CONFIG_DEV_LED. */
  };
} node_config_dev_t;

/** @brief A complete, validated node config. Holds no pointers. */
typedef struct {
  int version;                          /**< Config format version; always 1. */
  char node_name[NODE_CONFIG_NAME_LEN]; /**< NUL-terminated node name. */
  int n_i2c; /**< Number of valid entries in @c i2c. */
  node_config_i2c_bus_t i2c[NODE_CONFIG_MAX_I2C_BUSES]; /**< I2C buses. */
  int n_dev; /**< Number of valid entries in @c dev. */
  node_config_dev_t dev[NODE_CONFIG_MAX_DEVICES]; /**< Devices, in config
                                                      order. */
} node_config_t;

/** @brief Result of node_config_parse(). */
typedef enum {
  NODE_CONFIG_OK = 0,        /**< The config is valid. */
  NODE_CONFIG_ERR_ARG,       /**< NULL pointer or bad argument from the
                                caller. */
  NODE_CONFIG_ERR_TOO_LARGE, /**< Input longer than NODE_CONFIG_MAX_RAW_LEN. */
  NODE_CONFIG_ERR_SYNTAX,    /**< Malformed JSON. */
  NODE_CONFIG_ERR_MISSING,   /**< A required key is absent. */
  NODE_CONFIG_ERR_TYPE,      /**< A value has the wrong JSON type. */
  NODE_CONFIG_ERR_RANGE,     /**< A value is out of range, or too long. */
  NODE_CONFIG_ERR_VERSION,   /**< Unsupported "version". */
  NODE_CONFIG_ERR_DUPLICATE, /**< An id, GPIO or I2C port is used twice. */
  NODE_CONFIG_ERR_PIN,       /**< A pin rule is violated. */
  NODE_CONFIG_ERR_DEVICE_TYPE, /**< Unknown device "type". */
  NODE_CONFIG_ERR_BUS_REF,     /**< A device "bus" names no bus. */
  NODE_CONFIG_ERR_TOO_MANY,    /**< More buses or devices than the maximum. */
  NODE_CONFIG_ERR_NO_MEMORY,   /**< cJSON could not allocate. */
} node_config_err_code_t;

/** @brief Details of a rejected config. */
typedef struct {
  node_config_err_code_t code;         /**< What went wrong. */
  char path[NODE_CONFIG_ERR_PATH_LEN]; /**< JSON path of the bad value, for
                                          example "devices[1].gpio"; empty
                                          when the error concerns the whole
                                          input. NUL-terminated. */
  char msg[NODE_CONFIG_ERR_MSG_LEN];   /**< Human-readable reason that
                                          includes the path. NUL-terminated. */
} node_config_err_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Parse and validate a raw JSON config.
 *
 * Input longer than NODE_CONFIG_MAX_RAW_LEN is rejected before parsing. The
 * first error found stops parsing. Unknown keys and strapping pins are logged
 * as warnings and do not fail. Omitted optional fields get their documented
 * defaults. Every allocation made while parsing is freed before returning.
 *
 * @param json Raw JSON text; need not be NUL-terminated. Must not be NULL.
 * @param len  Number of bytes in @p json.
 * @param out  Receives the config on success; zeroed on failure. Must not be
 *             NULL.
 * @param err  Receives the error details; code is NODE_CONFIG_OK and path and
 *             msg are empty on success. May be NULL if the caller only needs
 *             the return value.
 * @return NODE_CONFIG_OK, or the code of the first error found.
 */
node_config_err_code_t node_config_parse(const char *json, size_t len,
                                         node_config_t *out,
                                         node_config_err_t *err);

/**
 * @brief Hash of the node's device composition.
 *
 * Depends only on the number of devices and their types, in order. Ids,
 * pins, buses and intervals do not change it. Spec 10 stores it at
 * provisioning to detect a config that changes the mesh composition.
 *
 * @param cfg Parsed config; must not be NULL.
 * @return The hash, or 0 if @p cfg is NULL.
 */
uint32_t node_config_composition_hash(const node_config_t *cfg);

#ifdef __cplusplus
}
#endif
