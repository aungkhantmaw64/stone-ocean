#pragma once

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Bring up the node after the boot banner has been logged.
 *
 * Called once from app_main() in task context. Starts the status LED, then
 * loads the stored config and enters normal or safe mode; later specs add
 * drivers, the CLI and the mesh.
 */
void boot_sequence_run(void);
