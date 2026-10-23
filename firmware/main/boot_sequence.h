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
 * or safe mode; later specs add drivers, the CLI and the mesh.
 */
void boot_sequence_run(void);
