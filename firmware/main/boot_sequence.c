/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "boot_sequence.h"

#include "config_store_mode.h"
#include "node_hal_selftest.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void boot_sequence_run(void) {
  config_store_mode_boot();
  node_hal_selftest_start();
}
