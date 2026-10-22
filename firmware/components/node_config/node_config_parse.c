/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

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

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Keys accepted at the top level of a config. */
static const char *const g_root_keys[] = {"version", "node_name", "i2c_buses",
                                          "devices"};

/** @brief Keys accepted in an "i2c_buses" entry. */
static const char *const g_i2c_bus_keys[] = {"id", "port", "sda", "scl",
                                             "freq_hz"};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

static bool node_config_parse_is_blank(const char *text, size_t len,
                                       size_t from);
static node_config_err_code_t node_config_parse_root(const cJSON *root,
                                                     node_config_t *out,
                                                     node_config_err_t *err);
static node_config_err_code_t
node_config_parse_i2c_buses(const cJSON *root, node_config_t *out,
                            node_config_err_t *err);
static node_config_err_code_t
node_config_parse_i2c_bus(const cJSON *item, const char *path,
                          node_config_i2c_bus_t *bus, node_config_err_t *err);
static node_config_err_code_t node_config_parse_devices(const cJSON *root,
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
static node_config_err_code_t
node_config_parse_get_int(const cJSON *object, const char *parent,
                          const char *key, bool required, int min, int max,
                          int *value, node_config_err_t *err);
static node_config_err_code_t node_config_parse_get_u32(
    const cJSON *object, const char *parent, const char *key, bool required,
    uint32_t min, uint32_t max, uint32_t *value, node_config_err_t *err);
static node_config_err_code_t
node_config_parse_get_string(const cJSON *object, const char *parent,
                             const char *key, char *dst, size_t size,
                             node_config_err_t *err);

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
  return node_config_parse_devices(root, err);
}

static node_config_err_code_t
node_config_parse_i2c_buses(const cJSON *root, node_config_t *out,
                            node_config_err_t *err) {
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
 * several fields or entries (pins, duplicates) are checked once the whole
 * config is parsed. */
static node_config_err_code_t
node_config_parse_i2c_bus(const cJSON *item, const char *path,
                          node_config_i2c_bus_t *bus, node_config_err_t *err) {
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

static node_config_err_code_t
node_config_parse_devices(const cJSON *root, node_config_err_t *err) {
  const cJSON *devices = NULL;
  int count = 0;
  return node_config_parse_get_array(root, "", "devices", true,
                                     NODE_CONFIG_MAX_DEVICES, &devices, &count,
                                     err);
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
   * overflow. */
  double number = item->valuedouble;
  if (!(number >= (double)min && number <= (double)max)) {
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

static node_config_err_code_t
node_config_parse_get_int(const cJSON *object, const char *parent,
                          const char *key, bool required, int min, int max,
                          int *value, node_config_err_t *err) {
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

/* Strings are always required and must be non-empty. */
static node_config_err_code_t
node_config_parse_get_string(const cJSON *object, const char *parent,
                             const char *key, char *dst, size_t size,
                             node_config_err_t *err) {
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
