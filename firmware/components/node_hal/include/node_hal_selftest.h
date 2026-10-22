#pragma once

/**
 * @brief Start the HAL hardware self-test in its own task.
 *
 * ESP-IDF build only. Initialises I2C port 0 on the configured pins, logs
 * every address from 0x08 to 0x77 that acknowledges and whether the SHTC3
 * (0x70) answered, then logs button edges and toggles the configured output
 * pin every 500 ms, forever. Pins come from the NODE_HAL_SELFTEST_* Kconfig
 * options. Does nothing unless CONFIG_NODE_HAL_SELFTEST is enabled. Call
 * once, from task context.
 */
void node_hal_selftest_start(void);
