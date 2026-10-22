#pragma once

/**
 * @file node_dev_report.h
 * @brief Decides when a device's sample is published. Not part of the public
 *        interface.
 *
 * Pure logic on caller-owned state, so it runs unchanged on the host. Times
 * are node_hal_time_ms() values and may wrap around.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "node_ao.h"
#include "node_config.h"
#include "node_dev.h"

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief What was last published for one device. Zero it to start over. */
typedef struct {
  bool published;         /**< True once a sample of the device was
                             published. */
  node_dev_value_t value; /**< Last published value; valid while
                             @c published. */
  uint32_t ts_ms;         /**< HAL time of the last publish, in ms; valid
                             while @c published. */
} node_dev_report_state_t;

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Whether a new sample must be published.
 *
 * The first sample is always due. After that:
 * - NODE_DEV_SIG_READING follows @p policy: due when the temperature or the
 *   humidity differs from the last published value by at least its delta
 *   (any change, if the delta is 0 or less) and min_interval_ms has passed,
 *   or when max_interval_ms has passed.
 * - NODE_DEV_SIG_MOTION is due when motion differs from the last published
 *   value.
 * - NODE_DEV_SIG_ONOFF_CHANGED is due when on differs from the last
 *   published value.
 *
 * @param state  What was last published; must not be NULL.
 * @param sig    The driver's sample signal.
 * @param policy Report policy; required for NODE_DEV_SIG_READING, ignored
 *               otherwise.
 * @param sample The new sample; must not be NULL.
 * @param now_ms Current HAL time, in ms.
 * @return True if the sample must be published; false if not, or for a NULL
 *         argument or an unknown signal.
 */
bool node_dev_report_is_due(const node_dev_report_state_t *state,
                            node_ao_sig_t sig,
                            const node_config_report_t *policy,
                            const node_dev_value_t *sample, uint32_t now_ms);

/**
 * @brief Remember that a value was published.
 *
 * @param state  State to update; NULL is ignored.
 * @param value  The published value; NULL is ignored.
 * @param now_ms HAL time of the publish, in ms.
 */
void node_dev_report_record(node_dev_report_state_t *state,
                            const node_dev_value_t *value, uint32_t now_ms);
