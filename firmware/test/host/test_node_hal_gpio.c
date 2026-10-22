#include "node_hal_mock.h"
#include "unity.h"

#define BUTTON_PIN 4
#define LED_PIN 5
#define FIRST_INVALID_PIN 40
#define LOW 0
#define HIGH 1

static int g_callback_count = 0;
static int g_callback_pin = 0;
static void *g_callback_ctx = NULL;

static void record_callback(int pin, void *ctx) {
  g_callback_count++;
  g_callback_pin = pin;
  g_callback_ctx = ctx;
}

static void watch_button(node_hal_gpio_edge_t edge) {
  TEST_ASSERT_EQUAL_INT(NODE_HAL_OK,
                        node_hal_gpio_config(BUTTON_PIN, NODE_HAL_GPIO_IN,
                                             NODE_HAL_GPIO_PULL_NONE));
  TEST_ASSERT_EQUAL_INT(
      NODE_HAL_OK,
      node_hal_gpio_set_isr(BUTTON_PIN, edge, record_callback, NULL));
}

void setUp(void) {
  node_hal_mock_reset();
  g_callback_count = 0;
  g_callback_pin = -1;
  g_callback_ctx = NULL;
}

void tearDown(void) {}

/* ---- Reporting input edges ---- */

/* AC: specs/02-hal.md #3 */
void test_callback_runs_once_when_an_input_rises_and_rising_edges_are_watched(
    void) {
  watch_button(NODE_HAL_GPIO_EDGE_RISING);

  node_hal_gpio_mock_set_input(BUTTON_PIN, HIGH);

  TEST_ASSERT_EQUAL_INT(1, g_callback_count);
}

/* AC: specs/02-hal.md #3 */
void test_callback_does_not_run_when_the_input_falls_and_only_rising_edges_are_watched(
    void) {
  node_hal_gpio_mock_set_input(BUTTON_PIN, HIGH);
  watch_button(NODE_HAL_GPIO_EDGE_RISING);

  node_hal_gpio_mock_set_input(BUTTON_PIN, LOW);

  TEST_ASSERT_EQUAL_INT(0, g_callback_count);
}

/* AC: specs/02-hal.md #3 */
void test_callback_runs_on_both_rise_and_fall_when_any_edge_is_watched(void) {
  watch_button(NODE_HAL_GPIO_EDGE_ANY);

  node_hal_gpio_mock_set_input(BUTTON_PIN, HIGH);
  node_hal_gpio_mock_set_input(BUTTON_PIN, LOW);

  TEST_ASSERT_EQUAL_INT(2, g_callback_count);
}

/* AC: specs/02-hal.md #3 */
void test_callback_does_not_run_when_an_input_is_set_to_the_level_it_already_has(
    void) {
  watch_button(NODE_HAL_GPIO_EDGE_ANY);

  node_hal_gpio_mock_set_input(BUTTON_PIN, LOW);

  TEST_ASSERT_EQUAL_INT(0, g_callback_count);
}

/* AC: specs/02-hal.md #3 */
void test_callback_receives_the_pin_number_and_the_context_it_was_registered_with(
    void) {
  int context = 0;
  node_hal_gpio_config(BUTTON_PIN, NODE_HAL_GPIO_IN, NODE_HAL_GPIO_PULL_NONE);
  node_hal_gpio_set_isr(BUTTON_PIN, NODE_HAL_GPIO_EDGE_RISING, record_callback,
                        &context);

  node_hal_gpio_mock_set_input(BUTTON_PIN, HIGH);

  TEST_ASSERT_EQUAL_INT(BUTTON_PIN, g_callback_pin);
  TEST_ASSERT_EQUAL_PTR(&context, g_callback_ctx);
}

/* ---- Pin levels ---- */

/* AC: specs/02-hal.md #3 */
void test_input_with_a_pull_up_reads_high_before_anything_drives_it(void) {
  node_hal_gpio_config(BUTTON_PIN, NODE_HAL_GPIO_IN, NODE_HAL_GPIO_PULL_UP);

  TEST_ASSERT_EQUAL_INT(HIGH, node_hal_gpio_read(BUTTON_PIN));
}

/* AC: specs/02-hal.md #3 */
void test_output_reads_back_the_level_last_written(void) {
  node_hal_gpio_config(LED_PIN, NODE_HAL_GPIO_OUT, NODE_HAL_GPIO_PULL_NONE);

  node_hal_gpio_write(LED_PIN, HIGH);

  TEST_ASSERT_EQUAL_INT(HIGH, node_hal_gpio_read(LED_PIN));
}

/* ---- Rejecting invalid requests ---- */

/* AC: specs/02-hal.md #4 */
void test_pin_number_outside_0_to_39_is_rejected_by_every_pin_function(void) {
  const int invalid_pins[] = {-1, FIRST_INVALID_PIN};

  for (size_t i = 0; i < sizeof invalid_pins / sizeof invalid_pins[0]; i++) {
    int pin = invalid_pins[i];
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        NODE_HAL_ERR_ARG,
        node_hal_gpio_config(pin, NODE_HAL_GPIO_OUT, NODE_HAL_GPIO_PULL_NONE),
        "config");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_HAL_ERR_ARG,
                                  node_hal_gpio_write(pin, HIGH), "write");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, node_hal_gpio_read(pin), "read");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_HAL_ERR_ARG,
                                  node_hal_gpio_set_isr(pin,
                                                        NODE_HAL_GPIO_EDGE_ANY,
                                                        record_callback, NULL),
                                  "set_isr");
  }
}

/* AC: specs/02-hal.md #4 */
void test_unknown_direction_or_pull_setting_is_rejected(void) {
  const node_hal_gpio_dir_t unknown_dir = (node_hal_gpio_dir_t)99;
  const node_hal_gpio_pull_t unknown_pull = (node_hal_gpio_pull_t)99;

  TEST_ASSERT_EQUAL_INT(
      NODE_HAL_ERR_ARG,
      node_hal_gpio_config(BUTTON_PIN, unknown_dir, NODE_HAL_GPIO_PULL_NONE));
  TEST_ASSERT_EQUAL_INT(
      NODE_HAL_ERR_ARG,
      node_hal_gpio_config(BUTTON_PIN, NODE_HAL_GPIO_IN, unknown_pull));
}

/* AC: specs/02-hal.md #4 */
void test_watching_edges_without_a_callback_is_rejected(void) {
  node_hal_gpio_config(BUTTON_PIN, NODE_HAL_GPIO_IN, NODE_HAL_GPIO_PULL_NONE);

  node_hal_err_t err =
      node_hal_gpio_set_isr(BUTTON_PIN, NODE_HAL_GPIO_EDGE_ANY, NULL, NULL);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG, err);
}

/* AC: specs/02-hal.md #4 */
void test_writing_to_a_pin_not_set_up_as_output_is_rejected(void) {
  node_hal_gpio_config(BUTTON_PIN, NODE_HAL_GPIO_IN, NODE_HAL_GPIO_PULL_NONE);

  node_hal_err_t err = node_hal_gpio_write(BUTTON_PIN, HIGH);

  TEST_ASSERT_EQUAL_INT(NODE_HAL_ERR_ARG, err);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_callback_runs_once_when_an_input_rises_and_rising_edges_are_watched);
  RUN_TEST(
      test_callback_does_not_run_when_the_input_falls_and_only_rising_edges_are_watched);
  RUN_TEST(test_callback_runs_on_both_rise_and_fall_when_any_edge_is_watched);
  RUN_TEST(
      test_callback_does_not_run_when_an_input_is_set_to_the_level_it_already_has);
  RUN_TEST(
      test_callback_receives_the_pin_number_and_the_context_it_was_registered_with);
  RUN_TEST(test_input_with_a_pull_up_reads_high_before_anything_drives_it);
  RUN_TEST(test_output_reads_back_the_level_last_written);
  RUN_TEST(test_pin_number_outside_0_to_39_is_rejected_by_every_pin_function);
  RUN_TEST(test_unknown_direction_or_pull_setting_is_rejected);
  RUN_TEST(test_watching_edges_without_a_callback_is_rejected);
  RUN_TEST(test_writing_to_a_pin_not_set_up_as_output_is_rejected);
  return UNITY_END();
}
