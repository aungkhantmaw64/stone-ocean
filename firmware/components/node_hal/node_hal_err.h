#pragma once

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "esp_err.h"
#include "node_hal.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Translate a generic ESP-IDF error into a HAL error.
 *
 * @param err ESP-IDF error code.
 * @return NODE_HAL_OK, NODE_HAL_ERR_ARG, NODE_HAL_ERR_TIMEOUT, or
 * NODE_HAL_ERR_IO for any other failure.
 */
node_hal_err_t node_hal_err_from_esp(esp_err_t err);
