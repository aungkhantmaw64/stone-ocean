#pragma once

/**
 * @file node_dev_pir.h
 * @brief PIR motion sensor driver for the devices AO.
 *
 * A PIR device is a GPIO input whose level says whether motion is sensed.
 * Every edge interrupts; the ISR only posts to the devices AO, which samples
 * the pin once it has been stable for the device's debounce_ms and publishes
 * NODE_DEV_SIG_MOTION when the debounced state changes.
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
 * @brief Register the PIR driver for NODE_CONFIG_DEV_PIR devices.
 *
 * Task context, before node_dev_start(). Its samples are published as
 * NODE_DEV_SIG_MOTION.
 *
 * @return As for node_dev_register_driver().
 */
node_dev_err_t node_dev_pir_register(void);

#ifdef __cplusplus
}
#endif
