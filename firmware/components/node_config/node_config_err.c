/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "node_config_internal.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_config_err_clear(node_config_err_t *err) {
  if (err == NULL) {
    return;
  }
  (void)memset(err, 0, sizeof(*err));
  err->code = NODE_CONFIG_OK;
}

node_config_err_code_t node_config_err_set(node_config_err_t *err,
                                           node_config_err_code_t code,
                                           const char *path, const char *fmt,
                                           ...) {
  if (err == NULL) {
    return code;
  }
  node_config_err_clear(err);
  err->code = code;

  const char *safe_path = (path != NULL) ? path : "";
  char reason[NODE_CONFIG_ERR_MSG_LEN] = {0};
  if (fmt != NULL) {
    va_list args;
    va_start(args, fmt);
    /* A reason longer than the buffer is truncated on purpose. */
    (void)vsnprintf(reason, sizeof(reason), fmt, args);
    va_end(args);
  }

  /* Truncating path and msg to their buffers is deliberate: the error must
   * always fit node_config_err_t. Only an encoding error is handled. */
  int written = snprintf(err->path, sizeof(err->path), "%s", safe_path);
  if (written < 0) {
    err->path[0] = '\0';
  }
  if (safe_path[0] != '\0') {
    written = snprintf(err->msg, sizeof(err->msg), "%s: %s", safe_path, reason);
  } else {
    written = snprintf(err->msg, sizeof(err->msg), "%s", reason);
  }
  if (written < 0) {
    err->msg[0] = '\0';
  }
  return code;
}
