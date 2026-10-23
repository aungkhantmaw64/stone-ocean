#pragma once

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Bring up the node after the boot banner has been logged.
 *
 * Called once from app_main() in task context. Initialises NVS, logs the
 * reset reason and crash count and starts the reset button (spec 09), starts
 * the status LED's heartbeat, then loads the stored config and enters normal
 * or safe mode. In normal mode it starts the device drivers. In both modes
 * it then starts the BLE mesh node, when the build has Bluetooth (spec 10).
 */
void boot_sequence_run(void);
