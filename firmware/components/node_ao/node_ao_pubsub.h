#pragma once

/**
 * @file node_ao_pubsub.h
 * @brief Subscription table control used by the host port. Not part of the
 *        public interface.
 */

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

/**
 * @brief Forget every subscription.
 *
 * Host tests only, through node_ao_port_mock_reset(), while no thread
 * subscribes or publishes.
 */
void node_ao_pubsub_reset(void);
