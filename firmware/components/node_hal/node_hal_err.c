/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_hal_err.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

node_hal_err_t node_hal_err_from_esp(esp_err_t err) {
  switch (err) {
  case ESP_OK:
    return NODE_HAL_OK;
  case ESP_ERR_INVALID_ARG:
    return NODE_HAL_ERR_ARG;
  case ESP_ERR_TIMEOUT:
    return NODE_HAL_ERR_TIMEOUT;
  default:
    return NODE_HAL_ERR_IO;
  }
}
