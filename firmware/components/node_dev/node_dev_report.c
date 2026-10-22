/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "node_dev_report.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Whether a reading is due under a report policy.
 *
 * @param state  What was last published; must have a published value.
 * @param policy Report policy.
 * @param sample The new reading.
 * @param now_ms Current HAL time, in ms.
 * @return True if the reading must be published.
 */
static bool node_dev_report_reading_is_due(const node_dev_report_state_t *state,
                                           const node_config_report_t *policy,
                                           const node_dev_value_t *sample,
                                           uint32_t now_ms);

/**
 * @brief Whether a value moved far enough from the last published one.
 *
 * @param last  Last published value.
 * @param now   New value.
 * @param delta Smallest change that counts; 0 or less means any change.
 * @return True if the value changed by at least @p delta.
 */
static bool node_dev_report_changed_by(float last, float now, float delta);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

bool node_dev_report_is_due(const node_dev_report_state_t *state,
                            node_ao_sig_t sig,
                            const node_config_report_t *policy,
                            const node_dev_value_t *sample, uint32_t now_ms) {
  if ((state == NULL) || (sample == NULL)) {
    return false;
  }
  switch (sig) {
    case NODE_DEV_SIG_READING:
      if (policy == NULL) {
        return false;
      }
      return !state->published ||
             node_dev_report_reading_is_due(state, policy, sample, now_ms);
    case NODE_DEV_SIG_MOTION:
      return !state->published || (sample->motion != state->value.motion);
    case NODE_DEV_SIG_ONOFF_CHANGED:
      return !state->published || (sample->on != state->value.on);
    default:
      return false;
  }
}

void node_dev_report_record(node_dev_report_state_t *state,
                            const node_dev_value_t *value, uint32_t now_ms) {
  if ((state == NULL) || (value == NULL)) {
    return;
  }
  state->published = true;
  state->value = *value;
  state->ts_ms = now_ms;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static bool node_dev_report_reading_is_due(const node_dev_report_state_t *state,
                                           const node_config_report_t *policy,
                                           const node_dev_value_t *sample,
                                           uint32_t now_ms) {
  const uint32_t elapsed_ms = now_ms - state->ts_ms;
  if (elapsed_ms >= policy->max_interval_ms) {
    return true;
  }
  if (elapsed_ms < policy->min_interval_ms) {
    return false;
  }
  return node_dev_report_changed_by(state->value.temp_c, sample->temp_c,
                                    policy->delta_temp_c) ||
         node_dev_report_changed_by(state->value.rh_pct, sample->rh_pct,
                                    policy->delta_rh_pct);
}

static bool node_dev_report_changed_by(float last, float now, float delta) {
  const float change = fabsf(now - last);
  if (delta <= 0.0f) {
    return change > 0.0f;
  }
  return change >= delta;
}
