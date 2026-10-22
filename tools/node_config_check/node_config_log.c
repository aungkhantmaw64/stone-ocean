/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdarg.h>
#include <stdio.h>

#include "node_config_internal.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_config_log_warning(const char *path, const char *fmt, ...) {
  char reason[NODE_CONFIG_ERR_MSG_LEN] = {0};
  if (fmt != NULL) {
    va_list args;
    va_start(args, fmt);
    /* A reason longer than the buffer is truncated on purpose, as on the
     * target. */
    (void)vsnprintf(reason, sizeof(reason), fmt, args);
    va_end(args);
  }
  /* Nothing useful can be done if stderr fails. */
  (void)fprintf(stderr, "warning: %s: %s\n", (path != NULL) ? path : "",
                reason);
}
