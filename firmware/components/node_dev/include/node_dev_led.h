#pragma once

/**
 * @file node_dev_led.h
 * @brief LED driver for the devices AO.
 *
 * An LED device is a GPIO output. It is driven to its default_on state at
 * init and switched with node_dev_set_onoff(); the devices AO publishes
 * NODE_DEV_SIG_ONOFF_CHANGED when its state changes, and once after init.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Register the LED driver for NODE_CONFIG_DEV_LED devices.
 *
 * Task context, before node_dev_start(). Its samples are published as
 * NODE_DEV_SIG_ONOFF_CHANGED.
 *
 * @return As for node_dev_register_driver().
 */
node_dev_err_t node_dev_led_register(void);

#ifdef __cplusplus
}
#endif
