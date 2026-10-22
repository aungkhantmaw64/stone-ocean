#pragma once

/**
 * @file node_ao_pub_sigs.h
 * @brief Project-wide registry of published signal blocks.
 *
 * Every component that publishes events owns one block of
 * NODE_AO_PUB_SIGS_PER_COMPONENT signals and defines its published signals
 * from the start of that block, so published signals never collide across
 * the firmware. Add a new publisher's block at the end, the next
 * NODE_AO_PUB_SIGS_PER_COMPONENT above the last one. This header includes no
 * FreeRTOS or ESP-IDF headers.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_ao.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Published signals in each component's block. */
#define NODE_AO_PUB_SIGS_PER_COMPONENT 16

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief First published signal of each publishing component. */
enum {
  NODE_AO_PUB_SIGS_NODE_DEV =
      NODE_AO_SIG_PUB_FIRST, /**< Devices AO, spec 06. */
};

#ifdef __cplusplus
}
#endif
