#pragma once

/**
 * @file node_dev_shtc3_internal.h
 * @brief Test control of the SHTC3 driver. Not part of the public interface.
 */

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Forget every SHTC3 device's state, including the I2C device handle
 *        it reuses across init retries.
 *
 * Host tests only, after node_hal_mock_reset() has freed the handles, while
 * no thread uses node_dev.
 */
void node_dev_shtc3_reset(void);
