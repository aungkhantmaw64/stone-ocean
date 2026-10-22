#include "node_hal_mock.h"

static uint32_t now_ms;

void node_hal_time_mock_reset(void) { now_ms = 0; }

void node_hal_time_mock_advance(uint32_t ms) { now_ms += ms; }

void node_hal_time_mock_set(uint32_t ms) { now_ms = ms; }

uint32_t node_hal_time_ms(void) { return now_ms; }

void node_hal_time_delay_ms(uint32_t ms) { now_ms += ms; }
