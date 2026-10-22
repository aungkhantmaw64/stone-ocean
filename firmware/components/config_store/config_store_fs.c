/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "config_store_fs.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_littlefs.h"
#include "esp_log.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief ESP-IDF log tag of the config_store component. */
#define CONFIG_STORE_FS_LOG_TAG "config_store"

/** @brief Label of the LittleFS partition in partitions.csv. */
#define CONFIG_STORE_FS_PARTITION_LABEL "config"

/** @brief VFS mount point of the config partition. */
#define CONFIG_STORE_FS_BASE_PATH "/config"

/** @brief Size of a full path buffer, including the terminating NUL; fits
 *         the mount point, a slash and a LittleFS file name. */
#define CONFIG_STORE_FS_PATH_LEN 48

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Build the full VFS path of a file on the config partition.
 *
 * @param name File name without a directory; must not be NULL.
 * @param path Receives the NUL-terminated path; must not be NULL.
 * @param size Size of @p path in bytes.
 * @return True on success, false if the path does not fit.
 */
static bool config_store_fs_path(const char *name, char *path, size_t size);

/**
 * @brief Write all of @p data to @p file.
 *
 * @param file Open file; must not be NULL.
 * @param data Bytes to write; may be NULL only if @p len is 0.
 * @param len  Number of bytes in @p data.
 * @return True if every byte was written.
 */
static bool config_store_fs_write_all(FILE *file, const void *data, size_t len);

/**
 * @brief Write @p head and @p body to a new file at @p path and close it.
 *
 * @param path     Full path of the file to create or truncate.
 * @param head     First part; may be NULL only if @p head_len is 0.
 * @param head_len Number of bytes in @p head.
 * @param body     Second part; may be NULL only if @p body_len is 0.
 * @param body_len Number of bytes in @p body.
 * @return True if the file was written and closed without error.
 */
static bool config_store_fs_write_file(const char *path, const void *head,
                                       size_t head_len, const void *body,
                                       size_t body_len);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

config_store_err_t config_store_fs_init(void) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = CONFIG_STORE_FS_BASE_PATH,
      .partition_label = CONFIG_STORE_FS_PARTITION_LABEL,
      .format_if_mount_failed = true,
  };
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "mounting partition '%s' failed: %s",
             CONFIG_STORE_FS_PARTITION_LABEL, esp_err_to_name(err));
    return CONFIG_STORE_ERR_IO;
  }
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_fs_size(const char *name, size_t *out_size) {
  if (name == NULL || out_size == NULL) {
    return CONFIG_STORE_ERR_ARG;
  }
  char path[CONFIG_STORE_FS_PATH_LEN] = {0};
  if (!config_store_fs_path(name, path, sizeof(path))) {
    return CONFIG_STORE_ERR_ARG;
  }

  struct stat info = {0};
  if (stat(path, &info) != 0) {
    if (errno == ENOENT) {
      return CONFIG_STORE_ERR_NOT_FOUND;
    }
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "stat %s failed: errno %d", path, errno);
    return CONFIG_STORE_ERR_IO;
  }
  if (info.st_size < 0) {
    return CONFIG_STORE_ERR_IO;
  }
  *out_size = (size_t)info.st_size;
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_fs_read(const char *name, size_t offset,
                                        void *buf, size_t len) {
  if (name == NULL || (buf == NULL && len > 0) || offset > (size_t)LONG_MAX) {
    return CONFIG_STORE_ERR_ARG;
  }
  char path[CONFIG_STORE_FS_PATH_LEN] = {0};
  if (!config_store_fs_path(name, path, sizeof(path))) {
    return CONFIG_STORE_ERR_ARG;
  }

  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    if (errno == ENOENT) {
      return CONFIG_STORE_ERR_NOT_FOUND;
    }
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "open %s failed: errno %d", path, errno);
    return CONFIG_STORE_ERR_IO;
  }

  config_store_err_t result = CONFIG_STORE_OK;
  if (fseek(file, (long)offset, SEEK_SET) != 0) {
    result = CONFIG_STORE_ERR_IO;
  } else if (len > 0 && fread(buf, 1, len, file) != len) {
    result =
        (ferror(file) != 0) ? CONFIG_STORE_ERR_IO : CONFIG_STORE_ERR_CORRUPT;
  }
  /* The file was only read, so a close error cannot lose data. */
  (void)fclose(file);
  if (result == CONFIG_STORE_ERR_IO) {
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "read %s failed", path);
  }
  return result;
}

config_store_err_t config_store_fs_write_atomic(
    const char *name, const char *tmp_name, const void *head, size_t head_len,
    const void *body, size_t body_len) {
  if (name == NULL || tmp_name == NULL || (head == NULL && head_len > 0) ||
      (body == NULL && body_len > 0)) {
    return CONFIG_STORE_ERR_ARG;
  }
  char path[CONFIG_STORE_FS_PATH_LEN] = {0};
  char tmp_path[CONFIG_STORE_FS_PATH_LEN] = {0};
  if (!config_store_fs_path(name, path, sizeof(path)) ||
      !config_store_fs_path(tmp_name, tmp_path, sizeof(tmp_path))) {
    return CONFIG_STORE_ERR_ARG;
  }

  if (!config_store_fs_write_file(tmp_path, head, head_len, body, body_len)) {
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "write %s failed: errno %d", tmp_path,
             errno);
    /* Best-effort cleanup; the write error is what the caller needs. */
    (void)unlink(tmp_path);
    return CONFIG_STORE_ERR_IO;
  }
  /* lfs_rename() replaces an existing destination atomically. */
  if (rename(tmp_path, path) != 0) {
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "rename %s to %s failed: errno %d",
             tmp_path, path, errno);
    /* Best-effort cleanup; the rename error is what the caller needs. */
    (void)unlink(tmp_path);
    return CONFIG_STORE_ERR_IO;
  }
  return CONFIG_STORE_OK;
}

config_store_err_t config_store_fs_remove(const char *name) {
  if (name == NULL) {
    return CONFIG_STORE_ERR_ARG;
  }
  char path[CONFIG_STORE_FS_PATH_LEN] = {0};
  if (!config_store_fs_path(name, path, sizeof(path))) {
    return CONFIG_STORE_ERR_ARG;
  }
  if (unlink(path) != 0 && errno != ENOENT) {
    ESP_LOGE(CONFIG_STORE_FS_LOG_TAG, "remove %s failed: errno %d", path,
             errno);
    return CONFIG_STORE_ERR_IO;
  }
  return CONFIG_STORE_OK;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool config_store_fs_path(const char *name, char *path, size_t size) {
  int written = snprintf(path, size, "%s/%s", CONFIG_STORE_FS_BASE_PATH, name);
  return written > 0 && (size_t)written < size;
}

static bool config_store_fs_write_all(FILE *file, const void *data,
                                      size_t len) {
  if (len == 0) {
    return true;
  }
  return fwrite(data, 1, len, file) == len;
}

static bool config_store_fs_write_file(const char *path, const void *head,
                                       size_t head_len, const void *body,
                                       size_t body_len) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    return false;
  }
  bool written = config_store_fs_write_all(file, head, head_len) &&
                 config_store_fs_write_all(file, body, body_len);
  /* LittleFS commits the file to flash on close, so its result counts. */
  bool closed = (fclose(file) == 0);
  return written && closed;
}
