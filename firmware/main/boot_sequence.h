#pragma once

/**
 * @brief Bring up the node after the boot banner has been logged.
 *
 * Called once from app_main() in task context. Empty until later specs add
 * config loading, drivers, the CLI and the mesh.
 */
void boot_sequence_run(void);
