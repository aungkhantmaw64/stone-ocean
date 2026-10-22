/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include "unity.h"

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {}

void tearDown(void) {}

/* ---- Host test build ---- */

/* AC: specs/01-project-skeleton.md #3 */
void test_host_tests_are_built_without_esp_idf(void) {
#ifdef ESP_PLATFORM
  TEST_FAIL_MESSAGE("host tests must not be built with ESP-IDF");
#else
  TEST_PASS();
#endif
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_host_tests_are_built_without_esp_idf);
  return UNITY_END();
}
