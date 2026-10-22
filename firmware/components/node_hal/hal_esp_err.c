#include "hal_esp_err.h"

hal_err_t hal_err_from_esp(esp_err_t err) {
  switch (err) {
  case ESP_OK:
    return HAL_OK;
  case ESP_ERR_INVALID_ARG:
    return HAL_ERR_ARG;
  case ESP_ERR_TIMEOUT:
    return HAL_ERR_TIMEOUT;
  default:
    return HAL_ERR_IO;
  }
}
