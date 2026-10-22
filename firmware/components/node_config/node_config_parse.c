/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "node_config.h"
#include "node_config_internal.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief The only config format version this parser accepts. */
#define NODE_CONFIG_PARSE_VERSION 1

/** @brief Upper bound on the members of one JSON object or array. Every member
 *         takes at least two bytes of input (for example "1,"), so no valid
 *         input of NODE_CONFIG_MAX_RAW_LEN bytes has more. Bounds the loops. */
#define NODE_CONFIG_PARSE_MAX_MEMBERS (NODE_CONFIG_MAX_RAW_LEN / 2)

/** @brief Number of elements in a fixed-size array. */
#define NODE_CONFIG_PARSE_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/** @brief Shortest SHTC3 "sample_interval_ms" accepted, in ms. */
#define NODE_CONFIG_PARSE_MIN_SAMPLE_INTERVAL_MS 1000

/** @brief Default PIR "debounce_ms", in ms. */
#define NODE_CONFIG_PARSE_DEFAULT_DEBOUNCE_MS 50

/** @brief Default "report.min_interval_ms", in ms. */
#define NODE_CONFIG_PARSE_DEFAULT_MIN_INTERVAL_MS 5000

/** @brief Default "report.max_interval_ms", in ms. */
#define NODE_CONFIG_PARSE_DEFAULT_MAX_INTERVAL_MS 60000

/** @brief Default "report.delta_temp_c", in °C. */
#define NODE_CONFIG_PARSE_DEFAULT_DELTA_TEMP_C 0.5f

/** @brief Default "report.delta_rh_pct", in percent relative humidity. */
#define NODE_CONFIG_PARSE_DEFAULT_DELTA_RH_PCT 2.0f

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief How one value of a device "type" maps to the config. */
typedef struct {
  const char *name;            /**< Value of "type" in the JSON. */
  node_config_dev_type_t type; /**< Matching device type. */
  const char *const *keys;     /**< Keys accepted in an entry of this type. */
  size_t n_keys;               /**< Number of entries in @c keys. */
} node_config_parse_dev_kind_t;

/** @brief How one value of a PIR "pull" maps to the HAL. */
typedef struct {
  const char *name;          /**< Value of "pull" in the JSON. */
  node_hal_gpio_pull_t pull; /**< Matching pull resistor. */
} node_config_parse_pull_name_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Keys accepted at the top level of a config. */
static const char *const g_root_keys[] = {"version", "node_name", "i2c_buses",
                                          "devices"};

/** @brief Keys accepted in an "i2c_buses" entry. */
static const char *const g_i2c_bus_keys[] = {"id", "port", "sda", "scl",
                                             "freq_hz"};

/** @brief Keys accepted in a "shtc3" device entry. */
static const char *const g_shtc3_keys[] = {"id", "type", "bus",
                                           "sample_interval_ms", "report"};

/** @brief Keys accepted in a "pir" device entry. */
static const char *const g_pir_keys[] = {"id",          "type",        "gpio",
                                         "active_high", "debounce_ms", "pull"};

/** @brief Keys accepted in a "led" device entry. */
static const char *const g_led_keys[] = {"id", "type", "gpio", "active_high",
                                         "default_on"};

/** @brief Keys accepted in an SHTC3 "report" object. */
static const char *const g_report_keys[] = {
    "min_interval_ms", "max_interval_ms", "delta_temp_c", "delta_rh_pct"};

/** @brief Every device "type" the parser knows. */
static const node_config_parse_dev_kind_t g_dev_kinds[] = {
    {"shtc3", NODE_CONFIG_DEV_SHTC3, g_shtc3_keys,
     NODE_CONFIG_PARSE_COUNT_OF(g_shtc3_keys)},
    {"pir", NODE_CONFIG_DEV_PIR, g_pir_keys,
     NODE_CONFIG_PARSE_COUNT_OF(g_pir_keys)},
    {"led", NODE_CONFIG_DEV_LED, g_led_keys,
     NODE_CONFIG_PARSE_COUNT_OF(g_led_keys)},
};

/** @brief Every PIR "pull" value the parser knows. */
static const node_config_parse_pull_name_t g_pull_names[] = {
    {"none", NODE_HAL_GPIO_PULL_NONE},
    {"up", NODE_HAL_GPIO_PULL_UP},
    {"down", NODE_HAL_GPIO_PULL_DOWN},
};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

static bool node_config_parse_is_blank(const char *text, size_t len,
                                       size_t from);
static node_config_err_code_t node_config_parse_root(const cJSON *root,
                                                     node_config_t *out,
                                                     node_config_err_t *err);
static node_config_err_code_t node_config_parse_i2c_buses(
    const cJSON *root, node_config_t *out, node_config_err_t *err);
static node_config_err_code_t node_config_parse_i2c_bus(
    const cJSON *item, const char *path, node_config_i2c_bus_t *bus,
    node_config_err_t *err);
static node_config_err_code_t node_config_parse_devices(const cJSON *root,
                                                        node_config_t *out,
                                                        node_config_err_t *err);
static node_config_err_code_t node_config_parse_device(const cJSON *item,
                                                       const char *path,
                                                       const node_config_t *cfg,
                                                       node_config_dev_t *dev,
                                                       node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_dev_kind(
    const cJSON *item, const char *path,
    const node_config_parse_dev_kind_t **kind, node_config_err_t *err);
static node_config_err_code_t node_config_parse_shtc3(
    const cJSON *item, const char *path, const node_config_t *cfg,
    node_config_shtc3_t *shtc3, node_config_err_t *err);
static node_config_err_code_t node_config_parse_report(
    const cJSON *item, const char *path, node_config_report_t *report,
    node_config_err_t *err);
static node_config_err_code_t node_config_parse_pir(const cJSON *item,
                                                    const char *path,
                                                    node_config_pir_t *pir,
                                                    node_config_err_t *err);
static node_config_err_code_t node_config_parse_led(const cJSON *item,
                                                    const char *path,
                                                    node_config_led_t *led,
                                                    node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_bus_index(
    const cJSON *item, const char *path, const node_config_t *cfg,
    int *bus_index, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_pull(
    const cJSON *item, const char *path, node_hal_gpio_pull_t *pull,
    node_config_err_t *err);
static void node_config_parse_warn_unknown_keys(const cJSON *object,
                                                const char *path,
                                                const char *const *known,
                                                size_t n_known);
static bool node_config_parse_key_is_known(const char *key,
                                           const char *const *known,
                                           size_t n_known);
static void node_config_parse_join_key(char *dst, size_t size,
                                       const char *parent, const char *key);
static void node_config_parse_join_index(char *dst, size_t size,
                                         const char *parent, int index);
static node_config_err_code_t node_config_parse_get_array(
    const cJSON *object, const char *parent, const char *key, bool required,
    int max_count, const cJSON **array, int *count, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_integer(
    const cJSON *object, const char *parent, const char *key, bool required,
    int64_t min, int64_t max, int64_t *value, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_int(
    const cJSON *object, const char *parent, const char *key, bool required,
    int min, int max, int *value, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_u32(
    const cJSON *object, const char *parent, const char *key, bool required,
    uint32_t min, uint32_t max, uint32_t *value, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_float(
    const cJSON *object, const char *parent, const char *key, float *value,
    node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_bool(
    const cJSON *object, const char *parent, const char *key, bool required,
    bool *value, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_string(
    const cJSON *object, const char *parent, const char *key, char *dst,
    size_t size, node_config_err_t *err);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_config_err_code_t node_config_parse(const char *json, size_t len,
                                         node_config_t *out,
                                         node_config_err_t *err) {
  node_config_err_clear(err);
  if (out == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_ARG, NULL,
                               "no output config given");
  }
  (void)memset(out, 0, sizeof(*out));
  if (json == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_ARG, NULL,
                               "no config text given");
  }
  if (len > NODE_CONFIG_MAX_RAW_LEN) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TOO_LARGE, NULL,
                               "config is %zu bytes, at most %d allowed", len,
                               NODE_CONFIG_MAX_RAW_LEN);
  }

  const char *parse_end = NULL;
  /* The per-call end pointer is used instead of cJSON_GetErrorPtr(), which is
   * global state shared between tasks. cJSON's own trailing-data check needs
   * a NUL inside the buffer, so trailing data is checked here instead. */
  cJSON *root = cJSON_ParseWithLengthOpts(json, len, &parse_end, false);
  size_t offset =
      (parse_end != NULL && parse_end >= json) ? (size_t)(parse_end - json) : 0;
  if (root != NULL && !node_config_parse_is_blank(json, len, offset)) {
    cJSON_Delete(root);
    root = NULL;
  }
  if (root == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_SYNTAX, NULL,
                               "malformed JSON near byte %zu", offset);
  }

  node_config_err_code_t code = node_config_parse_root(root, out, err);
  cJSON_Delete(root);
  if (code == NODE_CONFIG_OK) {
    code = node_config_validate(out, err);
  }
  if (code != NODE_CONFIG_OK) {
    (void)memset(out, 0, sizeof(*out));
  }
  return code;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

/* A NUL ends the text early, as for a C string stored in a larger buffer. */
static bool node_config_parse_is_blank(const char *text, size_t len,
                                       size_t from) {
  for (size_t i = from; i < len; i++) {
    char c = text[i];
    if (c == '\0') {
      return true;
    }
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      return false;
    }
  }
  return true;
}

static node_config_err_code_t node_config_parse_root(const cJSON *root,
                                                     node_config_t *out,
                                                     node_config_err_t *err) {
  if (!cJSON_IsObject(root)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, NULL,
                               "config must be a JSON object");
  }
  node_config_parse_warn_unknown_keys(root, "", g_root_keys,
                                      NODE_CONFIG_PARSE_COUNT_OF(g_root_keys));

  node_config_err_code_t code = node_config_parse_get_int(
      root, "", "version", true, INT_MIN, INT_MAX, &out->version, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }
  if (out->version != NODE_CONFIG_PARSE_VERSION) {
    return node_config_err_set(err, NODE_CONFIG_ERR_VERSION, "version",
                               "%d is not supported, expected %d", out->version,
                               NODE_CONFIG_PARSE_VERSION);
  }

  code = node_config_parse_get_string(root, "", "node_name", out->node_name,
                                      sizeof(out->node_name), err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  code = node_config_parse_i2c_buses(root, out, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }
  return node_config_parse_devices(root, out, err);
}

static node_config_err_code_t node_config_parse_i2c_buses(
    const cJSON *root, node_config_t *out, node_config_err_t *err) {
  const cJSON *buses = NULL;
  int count = 0;
  node_config_err_code_t code = node_config_parse_get_array(
      root, "", "i2c_buses", false, NODE_CONFIG_MAX_I2C_BUSES, &buses, &count,
      err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  for (int i = 0; i < count; i++) {
    char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
    node_config_parse_join_index(path, sizeof(path), "i2c_buses", i);
    code = node_config_parse_i2c_bus(cJSON_GetArrayItem(buses, i), path,
                                     &out->i2c[i], err);
    if (code != NODE_CONFIG_OK) {
      return code;
    }
    out->n_i2c = i + 1;
  }
  return NODE_CONFIG_OK;
}

/* Port and frequency limits are checked here, per field. Rules that relate
 * several fields or entries (pins, duplicates) are checked by
 * node_config_validate() once the whole config is parsed. */
static node_config_err_code_t node_config_parse_i2c_bus(
    const cJSON *item, const char *path, node_config_i2c_bus_t *bus,
    node_config_err_t *err) {
  if (!cJSON_IsObject(item)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be an object");
  }
  node_config_parse_warn_unknown_keys(
      item, path, g_i2c_bus_keys, NODE_CONFIG_PARSE_COUNT_OF(g_i2c_bus_keys));

  node_config_err_code_t code = node_config_parse_get_string(
      item, path, "id", bus->id, sizeof(bus->id), err);
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_int(item, path, "port", true, 0, 1, &bus->port,
                                     err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_int(item, path, "sda", true, 0, INT_MAX,
                                     &bus->sda, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_int(item, path, "scl", true, 0, INT_MAX,
                                     &bus->scl, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_u32(item, path, "freq_hz", true, 10000, 400000,
                                     &bus->freq_hz, err);
  }
  return code;
}

static node_config_err_code_t node_config_parse_devices(
    const cJSON *root, node_config_t *out, node_config_err_t *err) {
  const cJSON *devices = NULL;
  int count = 0;
  node_config_err_code_t code = node_config_parse_get_array(
      root, "", "devices", true, NODE_CONFIG_MAX_DEVICES, &devices, &count,
      err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  for (int i = 0; i < count; i++) {
    char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
    node_config_parse_join_index(path, sizeof(path), "devices", i);
    code = node_config_parse_device(cJSON_GetArrayItem(devices, i), path, out,
                                    &out->dev[i], err);
    if (code != NODE_CONFIG_OK) {
      return code;
    }
    out->n_dev = i + 1;
  }
  return NODE_CONFIG_OK;
}

/* The type is read first, so that unknown keys are judged against the keys of
 * that type. */
static node_config_err_code_t node_config_parse_device(const cJSON *item,
                                                       const char *path,
                                                       const node_config_t *cfg,
                                                       node_config_dev_t *dev,
                                                       node_config_err_t *err) {
  if (!cJSON_IsObject(item)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be an object");
  }
  const node_config_parse_dev_kind_t *kind = NULL;
  node_config_err_code_t code =
      node_config_parse_get_dev_kind(item, path, &kind, err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }
  node_config_parse_warn_unknown_keys(item, path, kind->keys, kind->n_keys);
  dev->type = kind->type;

  code = node_config_parse_get_string(item, path, "id", dev->id,
                                      sizeof(dev->id), err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }

  switch (kind->type) {
    case NODE_CONFIG_DEV_SHTC3:
      return node_config_parse_shtc3(item, path, cfg, &dev->shtc3, err);
    case NODE_CONFIG_DEV_PIR:
      return node_config_parse_pir(item, path, &dev->pir, err);
    case NODE_CONFIG_DEV_LED:
      return node_config_parse_led(item, path, &dev->led, err);
    default:
      return node_config_err_set(err, NODE_CONFIG_ERR_DEVICE_TYPE, path,
                                 "unsupported device type");
  }
}

static node_config_err_code_t node_config_parse_get_dev_kind(
    const cJSON *item, const char *path,
    const node_config_parse_dev_kind_t **kind, node_config_err_t *err) {
  char type_path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(type_path, sizeof(type_path), path, "type");

  const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
  if (type == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_MISSING, type_path,
                               "is required");
  }
  const char *name = cJSON_GetStringValue(type);
  if (name == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, type_path,
                               "must be a string");
  }
  for (size_t i = 0; i < NODE_CONFIG_PARSE_COUNT_OF(g_dev_kinds); i++) {
    if (strcmp(name, g_dev_kinds[i].name) == 0) {
      *kind = &g_dev_kinds[i];
      return NODE_CONFIG_OK;
    }
  }
  return node_config_err_set(err, NODE_CONFIG_ERR_DEVICE_TYPE, type_path,
                             "unknown device type \"%.*s\"", NODE_CONFIG_ID_LEN,
                             name);
}

static node_config_err_code_t node_config_parse_shtc3(
    const cJSON *item, const char *path, const node_config_t *cfg,
    node_config_shtc3_t *shtc3, node_config_err_t *err) {
  node_config_err_code_t code =
      node_config_parse_get_bus_index(item, path, cfg, &shtc3->bus_index, err);
  if (code == NODE_CONFIG_OK) {
    code =
        node_config_parse_get_u32(item, path, "sample_interval_ms", true,
                                  NODE_CONFIG_PARSE_MIN_SAMPLE_INTERVAL_MS,
                                  UINT32_MAX, &shtc3->sample_interval_ms, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_report(item, path, &shtc3->report, err);
  }
  return code;
}

/* The order of min and max is checked by node_config_validate(). */
static node_config_err_code_t node_config_parse_report(
    const cJSON *item, const char *path, node_config_report_t *report,
    node_config_err_t *err) {
  report->min_interval_ms = NODE_CONFIG_PARSE_DEFAULT_MIN_INTERVAL_MS;
  report->max_interval_ms = NODE_CONFIG_PARSE_DEFAULT_MAX_INTERVAL_MS;
  report->delta_temp_c = NODE_CONFIG_PARSE_DEFAULT_DELTA_TEMP_C;
  report->delta_rh_pct = NODE_CONFIG_PARSE_DEFAULT_DELTA_RH_PCT;

  const cJSON *object = cJSON_GetObjectItemCaseSensitive(item, "report");
  if (object == NULL) {
    return NODE_CONFIG_OK;
  }
  char report_path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(report_path, sizeof(report_path), path, "report");
  if (!cJSON_IsObject(object)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, report_path,
                               "must be an object");
  }
  node_config_parse_warn_unknown_keys(
      object, report_path, g_report_keys,
      NODE_CONFIG_PARSE_COUNT_OF(g_report_keys));

  node_config_err_code_t code =
      node_config_parse_get_u32(object, report_path, "min_interval_ms", false,
                                0, UINT32_MAX, &report->min_interval_ms, err);
  if (code == NODE_CONFIG_OK) {
    code =
        node_config_parse_get_u32(object, report_path, "max_interval_ms", false,
                                  0, UINT32_MAX, &report->max_interval_ms, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_float(object, report_path, "delta_temp_c",
                                       &report->delta_temp_c, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_float(object, report_path, "delta_rh_pct",
                                       &report->delta_rh_pct, err);
  }
  return code;
}

static node_config_err_code_t node_config_parse_pir(const cJSON *item,
                                                    const char *path,
                                                    node_config_pir_t *pir,
                                                    node_config_err_t *err) {
  pir->debounce_ms = NODE_CONFIG_PARSE_DEFAULT_DEBOUNCE_MS;
  pir->pull = NODE_HAL_GPIO_PULL_NONE;

  node_config_err_code_t code = node_config_parse_get_int(
      item, path, "gpio", true, 0, INT_MAX, &pir->gpio, err);
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_bool(item, path, "active_high", true,
                                      &pir->active_high, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_u32(item, path, "debounce_ms", false, 0,
                                     UINT32_MAX, &pir->debounce_ms, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_pull(item, path, &pir->pull, err);
  }
  return code;
}

static node_config_err_code_t node_config_parse_led(const cJSON *item,
                                                    const char *path,
                                                    node_config_led_t *led,
                                                    node_config_err_t *err) {
  led->default_on = false;

  node_config_err_code_t code = node_config_parse_get_int(
      item, path, "gpio", true, 0, INT_MAX, &led->gpio, err);
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_bool(item, path, "active_high", true,
                                      &led->active_high, err);
  }
  if (code == NODE_CONFIG_OK) {
    code = node_config_parse_get_bool(item, path, "default_on", false,
                                      &led->default_on, err);
  }
  return code;
}

/* Buses are parsed before devices, so the reference can be resolved here. */
static node_config_err_code_t node_config_parse_get_bus_index(
    const cJSON *item, const char *path, const node_config_t *cfg,
    int *bus_index, node_config_err_t *err) {
  char bus_id[NODE_CONFIG_ID_LEN] = {0};
  node_config_err_code_t code = node_config_parse_get_string(
      item, path, "bus", bus_id, sizeof(bus_id), err);
  if (code != NODE_CONFIG_OK) {
    return code;
  }
  for (int i = 0; i < cfg->n_i2c && i < NODE_CONFIG_MAX_I2C_BUSES; i++) {
    if (strcmp(bus_id, cfg->i2c[i].id) == 0) {
      *bus_index = i;
      return NODE_CONFIG_OK;
    }
  }
  char bus_path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(bus_path, sizeof(bus_path), path, "bus");
  return node_config_err_set(err, NODE_CONFIG_ERR_BUS_REF, bus_path,
                             "no I2C bus has id \"%s\"", bus_id);
}

static node_config_err_code_t node_config_parse_get_pull(
    const cJSON *item, const char *path, node_hal_gpio_pull_t *pull,
    node_config_err_t *err) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "pull");
  if (value == NULL) {
    return NODE_CONFIG_OK;
  }
  char pull_path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(pull_path, sizeof(pull_path), path, "pull");
  const char *name = cJSON_GetStringValue(value);
  if (name == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, pull_path,
                               "must be a string");
  }
  for (size_t i = 0; i < NODE_CONFIG_PARSE_COUNT_OF(g_pull_names); i++) {
    if (strcmp(name, g_pull_names[i].name) == 0) {
      *pull = g_pull_names[i].pull;
      return NODE_CONFIG_OK;
    }
  }
  return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, pull_path,
                             "must be \"none\", \"up\" or \"down\"");
}

static void node_config_parse_warn_unknown_keys(const cJSON *object,
                                                const char *path,
                                                const char *const *known,
                                                size_t n_known) {
  const cJSON *child = (object != NULL) ? object->child : NULL;
  for (int i = 0; i < NODE_CONFIG_PARSE_MAX_MEMBERS && child != NULL; i++) {
    if (!node_config_parse_key_is_known(child->string, known, n_known)) {
      char key_path[NODE_CONFIG_ERR_PATH_LEN] = {0};
      node_config_parse_join_key(key_path, sizeof(key_path), path,
                                 (child->string != NULL) ? child->string : "");
      node_config_log_warning(key_path, "unknown key ignored");
    }
    child = child->next;
  }
}

static bool node_config_parse_key_is_known(const char *key,
                                           const char *const *known,
                                           size_t n_known) {
  if (key == NULL || known == NULL) {
    return false;
  }
  for (size_t i = 0; i < n_known; i++) {
    if (strcmp(key, known[i]) == 0) {
      return true;
    }
  }
  return false;
}

/* A path longer than the buffer is truncated; the error is still reported,
 * with a shortened path. */
static void node_config_parse_join_key(char *dst, size_t size,
                                       const char *parent, const char *key) {
  int written = 0;
  if (parent == NULL || parent[0] == '\0') {
    written = snprintf(dst, size, "%s", key);
  } else {
    written = snprintf(dst, size, "%s.%s", parent, key);
  }
  if (written < 0 && size > 0) {
    dst[0] = '\0';
  }
}

static void node_config_parse_join_index(char *dst, size_t size,
                                         const char *parent, int index) {
  int written = snprintf(dst, size, "%s[%d]", parent, index);
  if (written < 0 && size > 0) {
    dst[0] = '\0';
  }
}

static node_config_err_code_t node_config_parse_get_array(
    const cJSON *object, const char *parent, const char *key, bool required,
    int max_count, const cJSON **array, int *count, node_config_err_t *err) {
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(path, sizeof(path), parent, key);
  *array = NULL;
  *count = 0;

  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return required ? node_config_err_set(err, NODE_CONFIG_ERR_MISSING, path,
                                          "is required")
                    : NODE_CONFIG_OK;
  }
  if (!cJSON_IsArray(item)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be an array");
  }
  int size = cJSON_GetArraySize(item);
  if (size > max_count) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TOO_MANY, path,
                               "has %d entries, at most %d allowed", size,
                               max_count);
  }
  *array = item;
  *count = size;
  return NODE_CONFIG_OK;
}

/* Leaves *value unchanged when the key is absent and not required, so the
 * caller can preset the default. */
static node_config_err_code_t node_config_parse_get_integer(
    const cJSON *object, const char *parent, const char *key, bool required,
    int64_t min, int64_t max, int64_t *value, node_config_err_t *err) {
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(path, sizeof(path), parent, key);

  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return required ? node_config_err_set(err, NODE_CONFIG_ERR_MISSING, path,
                                          "is required")
                    : NODE_CONFIG_OK;
  }
  if (!cJSON_IsNumber(item)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be a number");
  }
  /* The range is checked on the double first, so the cast below cannot
   * overflow. A max of INT_MAX or UINT32_MAX means "no upper limit". */
  double number = item->valuedouble;
  if (!(number >= (double)min && number <= (double)max)) {
    if (max == INT_MAX || max == UINT32_MAX) {
      return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, path,
                                 "must be at least %" PRId64, min);
    }
    return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, path,
                               "must be between %" PRId64 " and %" PRId64, min,
                               max);
  }
  int64_t integer = (int64_t)number;
  if ((double)integer != number) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be a whole number");
  }
  *value = integer;
  return NODE_CONFIG_OK;
}

static node_config_err_code_t node_config_parse_get_int(
    const cJSON *object, const char *parent, const char *key, bool required,
    int min, int max, int *value, node_config_err_t *err) {
  int64_t wide = *value;
  node_config_err_code_t code = node_config_parse_get_integer(
      object, parent, key, required, min, max, &wide, err);
  if (code == NODE_CONFIG_OK) {
    *value = (int)wide;
  }
  return code;
}

static node_config_err_code_t node_config_parse_get_u32(
    const cJSON *object, const char *parent, const char *key, bool required,
    uint32_t min, uint32_t max, uint32_t *value, node_config_err_t *err) {
  int64_t wide = *value;
  node_config_err_code_t code = node_config_parse_get_integer(
      object, parent, key, required, min, max, &wide, err);
  if (code == NODE_CONFIG_OK) {
    *value = (uint32_t)wide;
  }
  return code;
}

/* Optional: leaves *value unchanged when the key is absent, so the caller can
 * preset the default. */
static node_config_err_code_t node_config_parse_get_float(
    const cJSON *object, const char *parent, const char *key, float *value,
    node_config_err_t *err) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return NODE_CONFIG_OK;
  }
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(path, sizeof(path), parent, key);
  if (!cJSON_IsNumber(item)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be a number");
  }
  double number = item->valuedouble;
  if (!(number >= -(double)FLT_MAX && number <= (double)FLT_MAX)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, path,
                               "is too large");
  }
  *value = (float)number;
  return NODE_CONFIG_OK;
}

/* Leaves *value unchanged when the key is absent and not required, so the
 * caller can preset the default. */
static node_config_err_code_t node_config_parse_get_bool(
    const cJSON *object, const char *parent, const char *key, bool required,
    bool *value, node_config_err_t *err) {
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(path, sizeof(path), parent, key);

  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return required ? node_config_err_set(err, NODE_CONFIG_ERR_MISSING, path,
                                          "is required")
                    : NODE_CONFIG_OK;
  }
  if (!cJSON_IsBool(item)) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be true or false");
  }
  *value = cJSON_IsTrue(item);
  return NODE_CONFIG_OK;
}

/* Strings are always required and must be non-empty. */
static node_config_err_code_t node_config_parse_get_string(
    const cJSON *object, const char *parent, const char *key, char *dst,
    size_t size, node_config_err_t *err) {
  char path[NODE_CONFIG_ERR_PATH_LEN] = {0};
  node_config_parse_join_key(path, sizeof(path), parent, key);

  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_MISSING, path,
                               "is required");
  }
  const char *text = cJSON_GetStringValue(item);
  if (text == NULL) {
    return node_config_err_set(err, NODE_CONFIG_ERR_TYPE, path,
                               "must be a string");
  }
  size_t length = strnlen(text, size);
  if (length == 0) {
    return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, path,
                               "must not be empty");
  }
  if (length >= size) {
    return node_config_err_set(err, NODE_CONFIG_ERR_RANGE, path,
                               "must be shorter than %zu characters", size);
  }
  (void)memcpy(dst, text, length);
  dst[length] = '\0';
  return NODE_CONFIG_OK;
}
