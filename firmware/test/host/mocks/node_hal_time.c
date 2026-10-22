#include "node_hal_mock.h"

static uint32_t g_now_ms;

void node_hal_time_mock_reset(void) { g_now_ms = 0; }

void node_hal_time_mock_advance(uint32_t ms) { g_now_ms += ms; }

void node_hal_time_mock_set(uint32_t ms) { g_now_ms = ms; }

uint32_t node_hal_time_ms(void) { return g_now_ms; }

void node_hal_time_delay_ms(uint32_t ms) { g_now_ms += ms; }
