/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "config_store_fs.h"
#include "config_store_fs_mock.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief One file held in memory. */
typedef struct {
  bool used;                                /**< True if the slot holds a
                                               file. */
  char name[CONFIG_STORE_FS_MOCK_NAME_LEN]; /**< NUL-terminated file name. */
  size_t len; /**< Number of valid bytes in @c data, at most
                 CONFIG_STORE_FS_MOCK_MAX_FILE_LEN. */
  uint8_t data[CONFIG_STORE_FS_MOCK_MAX_FILE_LEN]; /**< File contents. */
} config_store_fs_mock_file_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief File slots; written by the fake and by config_store_fs_mock_put().
 */
static config_store_fs_mock_file_t g_files[CONFIG_STORE_FS_MOCK_MAX_FILES] = {
    0};

/** @brief Result forced on each operation, indexed by
 *         config_store_fs_mock_op_t; CONFIG_STORE_OK means not forced. */
static config_store_err_t g_forced_errors[CONFIG_STORE_FS_MOCK_OP_COUNT] = {
    CONFIG_STORE_OK};

/** @brief Calls to config_store_fs_init() since the last reset. */
static size_t g_init_count = 0;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Slot holding the file called @p name.
 *
 * @param name File name; NULL finds nothing.
 * @return The slot, or NULL if no such file exists.
 */
static config_store_fs_mock_file_t *config_store_fs_find(const char *name);

/**
 * @brief Slot for the file called @p name, claiming a free one if needed.
 *
 * @param name File name that fits CONFIG_STORE_FS_MOCK_NAME_LEN.
 * @return The slot, or NULL if the file is new and every slot is used.
 */
static config_store_fs_mock_file_t *config_store_fs_find_or_add(
    const char *name);

/**
 * @brief Whether @p name is a usable file name for the fake.
 *
 * @param name File name.
 * @return True if @p name is non-NULL, non-empty and shorter than
 *         CONFIG_STORE_FS_MOCK_NAME_LEN.
 */
static bool config_store_fs_name_fits(const char *name);

/**
 * @brief Error forced on @p op by the test.
 *
 * @param op Operation being run.
 * @return The forced error, or CONFIG_STORE_OK if none is forced.
 */
static config_store_err_t config_store_fs_forced(config_store_fs_mock_op_t op);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void config_store_fs_mock_reset(void) {
  (void)memset(g_files, 0, sizeof(g_files));
  for (size_t i = 0; i < CONFIG_STORE_FS_MOCK_OP_COUNT; i++) {
    g_forced_errors[i] = CONFIG_STORE_OK;
  }
  g_init_count = 0;
}

void config_store_fs_mock_set_error(config_store_fs_mock_op_t op,
                                    config_store_err_t err) {
  if ((unsigned)op >= (unsigned)CONFIG_STORE_FS_MOCK_OP_COUNT) {
    return;
  }
  g_forced_errors[op] = err;
}

int config_store_fs_mock_put(const char *name, const void *data, size_t len) {
  if (!config_store_fs_name_fits(name) || (data == NULL && len > 0) ||
      len > CONFIG_STORE_FS_MOCK_MAX_FILE_LEN) {
    return -1;
  }
  config_store_fs_mock_file_t *file = config_store_fs_find_or_add(name);
  if (file == NULL) {
    return -1;
  }
  if (len > 0) {
    (void)memcpy(file->data, data, len);
  }
  file->len = len;
  return 0;
}

uint8_t *config_store_fs_mock_file(const char *name, size_t *out_len) {
  config_store_fs_mock_file_t *file = config_store_fs_find(name);
  if (file == NULL) {
    return NULL;
  }
  if (out_len != NULL) {
    *out_len = file->len;
  }
  return file->data;
}

size_t config_store_fs_mock_init_count(void) { return g_init_count; }

config_store_err_t config_store_fs_init(void) {
  if (g_init_count < SIZE_MAX) {
    g_init_count++;
  }
  return config_store_fs_forced(CONFIG_STORE_FS_MOCK_OP_INIT);
}

config_store_err_t config_store_fs_size(const char *name, size_t *out_size) {
  if (name == NULL || out_size == NULL) {
    return CONFIG_STORE_ERR_ARG;
  }
  config_store_err_t forced =
      config_store_fs_forced(CONFIG_STORE_FS_MOCK_OP_SIZE);
  if (forced != CONFIG_STORE_OK) {
    return forced;
  }
  const config_store_fs_mock_file_t *file = config_store_fs_find(name);
  if (file == NULL) {
    return CONFIG_STORE_ERR_NOT_FOUND;
  }
  *out_size = file->len;
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_fs_read(const char *name, size_t offset,
                                        void *buf, size_t len) {
  if (name == NULL || (buf == NULL && len > 0)) {
    return CONFIG_STORE_ERR_ARG;
  }
  config_store_err_t forced =
      config_store_fs_forced(CONFIG_STORE_FS_MOCK_OP_READ);
  if (forced != CONFIG_STORE_OK) {
    return forced;
  }
  const config_store_fs_mock_file_t *file = config_store_fs_find(name);
  if (file == NULL) {
    return CONFIG_STORE_ERR_NOT_FOUND;
  }
  if (offset > file->len || len > file->len - offset) {
    return CONFIG_STORE_ERR_CORRUPT;
  }
  if (len > 0) {
    (void)memcpy(buf, &file->data[offset], len);
  }
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_fs_write_atomic(
    const char *name, const char *tmp_name, const void *head, size_t head_len,
    const void *body, size_t body_len) {
  if (name == NULL || tmp_name == NULL || (head == NULL && head_len > 0) ||
      (body == NULL && body_len > 0)) {
    return CONFIG_STORE_ERR_ARG;
  }
  config_store_err_t forced =
      config_store_fs_forced(CONFIG_STORE_FS_MOCK_OP_WRITE);
  if (forced != CONFIG_STORE_OK) {
    return forced;
  }
  if (!config_store_fs_name_fits(name) ||
      head_len > CONFIG_STORE_FS_MOCK_MAX_FILE_LEN ||
      body_len > CONFIG_STORE_FS_MOCK_MAX_FILE_LEN - head_len) {
    return CONFIG_STORE_ERR_IO;
  }
  config_store_fs_mock_file_t *file = config_store_fs_find_or_add(name);
  if (file == NULL) {
    return CONFIG_STORE_ERR_IO;
  }
  if (head_len > 0) {
    (void)memcpy(file->data, head, head_len);
  }
  if (body_len > 0) {
    (void)memcpy(&file->data[head_len], body, body_len);
  }
  file->len = head_len + body_len;
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_fs_remove(const char *name) {
  if (name == NULL) {
    return CONFIG_STORE_ERR_ARG;
  }
  config_store_err_t forced =
      config_store_fs_forced(CONFIG_STORE_FS_MOCK_OP_REMOVE);
  if (forced != CONFIG_STORE_OK) {
    return forced;
  }
  config_store_fs_mock_file_t *file = config_store_fs_find(name);
  if (file != NULL) {
    (void)memset(file, 0, sizeof(*file));
  }
  return CONFIG_STORE_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static config_store_fs_mock_file_t *config_store_fs_find(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < CONFIG_STORE_FS_MOCK_MAX_FILES; i++) {
    if (g_files[i].used &&
        strncmp(g_files[i].name, name, sizeof(g_files[i].name)) == 0) {
      return &g_files[i];
    }
  }
  return NULL;
}

static config_store_fs_mock_file_t *config_store_fs_find_or_add(
    const char *name) {
  config_store_fs_mock_file_t *file = config_store_fs_find(name);
  if (file != NULL) {
    return file;
  }
  for (size_t i = 0; i < CONFIG_STORE_FS_MOCK_MAX_FILES; i++) {
    if (!g_files[i].used) {
      g_files[i].used = true;
      g_files[i].len = 0;
      /* The caller has checked that the name fits, so the copy, NUL
       * included, stays inside the buffer. */
      size_t name_len = strnlen(name, sizeof(g_files[i].name) - 1);
      (void)memcpy(g_files[i].name, name, name_len);
      g_files[i].name[name_len] = '\0';
      return &g_files[i];
    }
  }
  return NULL;
}

static bool config_store_fs_name_fits(const char *name) {
  if (name == NULL) {
    return false;
  }
  size_t len = strnlen(name, CONFIG_STORE_FS_MOCK_NAME_LEN);
  return len > 0 && len < CONFIG_STORE_FS_MOCK_NAME_LEN;
}

static config_store_err_t config_store_fs_forced(config_store_fs_mock_op_t op) {
  if ((unsigned)op >= (unsigned)CONFIG_STORE_FS_MOCK_OP_COUNT) {
    return CONFIG_STORE_OK;
  }
  return g_forced_errors[op];
}
