#include "node_hal_mock.h"

void node_hal_mock_reset(void) {
  node_hal_gpio_mock_reset();
  node_hal_i2c_mock_reset();
  node_hal_time_mock_reset();
}
