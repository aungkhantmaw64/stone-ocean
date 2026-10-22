#pragma once

#include "esp_err.h"
#include "node_hal.h"

/**
 * @brief Translate a generic ESP-IDF error into a HAL error.
 *
 * @param err ESP-IDF error code.
 * @return HAL_OK, HAL_ERR_ARG, HAL_ERR_TIMEOUT, or HAL_ERR_IO for any other
 *         failure.
 */
hal_err_t hal_err_from_esp(esp_err_t err);
