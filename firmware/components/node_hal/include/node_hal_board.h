#pragma once

/**
 * @file node_hal_board.h
 * @brief Fixed parts of the node's board: their pins and polarity.
 *
 * These pins belong to the board, not to the config, so a config may never
 * use them. Header only and free of ESP-IDF headers, so the PC config checker
 * (tools/node_config_check) can include it.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief GPIO of the status LED (spec 16). A strapping pin: the LED is wired
 *         from 3.3 V so it never pulls the pin low at reset. */
#define NODE_HAL_BOARD_STATUS_LED_GPIO 0

/** @brief True if a high level lights the status LED; it is active low. */
#define NODE_HAL_BOARD_STATUS_LED_ACTIVE_HIGH false

/** @brief GPIO of the reset button (spec 09). Input-only; the board pulls it
 *         up externally and a press pulls it low. */
#define NODE_HAL_BOARD_RESET_BUTTON_GPIO 34
