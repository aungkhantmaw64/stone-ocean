#pragma once

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Whether a GPIO exists and is not reserved by flash, PSRAM or another
 *        driver.
 *
 * @param pin GPIO number.
 * @return true if the pin can be configured.
 */
bool node_hal_gpio_pin_is_usable(int pin);
