/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "config_store.h"
#include "config_store_fs_mock.h"
#include "node_ao_port_mock.h"
#include "node_hal_mock.h"
#include "node_health.h"
#include "node_health_internal.h"
#include "node_health_log_mock.h"
#include "node_health_port.h"
#include "node_health_port_mock.h"
#include "unity.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief A crash count left by earlier boots. */
#define EARLIER_CRASHES 3

/** @brief Size of the buffer a hook loads the stored config into, in bytes. */
#define CONFIG_BUF_LEN 64

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief A reset reason and the name the boot line must show for it. */
typedef struct {
  node_health_port_reason_t reason; /**< Reason from the port. */
  const char *name;                 /**< Expected name. */
} reason_name_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief A small config to store before a factory reset; any bytes do, as
 *         config_store does not parse them. */
static const char g_config[] = "{\"version\":1}";

/** @brief Every reset reason with its expected name (spec 09). */
static const reason_name_t g_reason_names[] = {
    {NODE_HEALTH_PORT_REASON_UNKNOWN, "unknown"},
    {NODE_HEALTH_PORT_REASON_POWERON, "poweron"},
    {NODE_HEALTH_PORT_REASON_EXT, "ext"},
    {NODE_HEALTH_PORT_REASON_SW, "sw"},
    {NODE_HEALTH_PORT_REASON_PANIC, "panic"},
    {NODE_HEALTH_PORT_REASON_INT_WDT, "int_wdt"},
    {NODE_HEALTH_PORT_REASON_TASK_WDT, "task_wdt"},
    {NODE_HEALTH_PORT_REASON_WDT, "wdt"},
    {NODE_HEALTH_PORT_REASON_DEEPSLEEP, "deepsleep"},
    {NODE_HEALTH_PORT_REASON_BROWNOUT, "brownout"},
    {NODE_HEALTH_PORT_REASON_SDIO, "sdio"},
};

/** @brief Reset reasons that count as crashes (spec 09). */
static const node_health_port_reason_t g_crash_reasons[] = {
    NODE_HEALTH_PORT_REASON_PANIC,
    NODE_HEALTH_PORT_REASON_INT_WDT,
    NODE_HEALTH_PORT_REASON_TASK_WDT,
    NODE_HEALTH_PORT_REASON_BROWNOUT,
};

/** @brief Reset reasons that do not count as crashes. */
static const node_health_port_reason_t g_non_crash_reasons[] = {
    NODE_HEALTH_PORT_REASON_UNKNOWN, NODE_HEALTH_PORT_REASON_POWERON,
    NODE_HEALTH_PORT_REASON_EXT,     NODE_HEALTH_PORT_REASON_SW,
    NODE_HEALTH_PORT_REASON_WDT,     NODE_HEALTH_PORT_REASON_DEEPSLEEP,
    NODE_HEALTH_PORT_REASON_SDIO,
};

/** @brief Ids of the hooks in the order they ran; reset in setUp(). */
static int g_hook_calls[NODE_HEALTH_MAX_RESET_HOOKS] = {0};

/** @brief Number of valid entries in g_hook_calls. */
static size_t g_hook_call_count = 0;

/** @brief Result of loading the stored config from inside a hook; reset in
 *         setUp(). */
static config_store_err_t g_config_seen_by_hook = CONFIG_STORE_ERR_IO;

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Start node_health after a reset for @p reason, and run its AO's
 *        init event.
 *
 * @param reason Reason the port reports.
 */
static void boot_after(node_health_port_reason_t reason);

/**
 * @brief Record that the hook with @p id ran.
 *
 * @param id Id of the hook.
 */
static void record_hook(int id);

/** @brief Erase hook with id 1. */
static void hook_one(void);

/** @brief Erase hook with id 2. */
static void hook_two(void);

/** @brief Erase hook with id 3. */
static void hook_three(void);

/** @brief Erase hook that checks whether the config is still stored. */
static void hook_that_loads_the_config(void);

/** @brief Store g_config, failing the test if it cannot be stored. */
static void store_config(void);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void setUp(void) {
  node_hal_mock_reset();
  node_ao_port_mock_reset();
  node_health_ao_reset();
  node_health_reset();
  node_health_port_mock_reset();
  node_health_log_mock_reset();
  config_store_fs_mock_reset();
  (void)memset(g_hook_calls, 0, sizeof(g_hook_calls));
  g_hook_call_count = 0;
  g_config_seen_by_hook = CONFIG_STORE_ERR_IO;
}

void tearDown(void) {}

/* ---- Reporting why the node reset ---- */

/* AC: specs/09-health-factory-reset.md #2 */
void test_every_reset_reason_is_logged_by_its_own_name_at_boot(void) {
  for (size_t i = 0; i < sizeof(g_reason_names) / sizeof(g_reason_names[0]);
       i++) {
    setUp();

    boot_after(g_reason_names[i].reason);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(g_reason_names[i].name,
                                     node_health_log_mock_last_boot_reason(),
                                     g_reason_names[i].name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g_reason_names[i].name,
                                     node_health_reset_reason_str(),
                                     g_reason_names[i].name);
  }
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_an_unrecognised_reset_reason_is_logged_as_unknown(void) {
  boot_after(NODE_HEALTH_PORT_REASON_COUNT);

  TEST_ASSERT_EQUAL_STRING("unknown", node_health_log_mock_last_boot_reason());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_reset_reason_is_unknown_before_the_health_module_starts(void) {
  TEST_ASSERT_EQUAL_STRING("unknown", node_health_reset_reason_str());
  TEST_ASSERT_EQUAL_UINT32(0, node_health_crash_count());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_boot_line_is_logged_only_once_even_if_the_health_module_is_started_twice(
    void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);
  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  node_health_init();

  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_boot_count());
  TEST_ASSERT_EQUAL_UINT32(EARLIER_CRASHES + 1,
                           node_health_port_mock_counter());
}

/* ---- Counting crashes ---- */

/* AC: specs/09-health-factory-reset.md #1 */
void test_crash_count_goes_up_by_one_after_a_task_watchdog_reset(void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);

  boot_after(NODE_HEALTH_PORT_REASON_TASK_WDT);

  TEST_ASSERT_EQUAL_UINT32(EARLIER_CRASHES + 1,
                           node_health_log_mock_last_boot_crash_count());
  TEST_ASSERT_EQUAL_UINT32(EARLIER_CRASHES + 1, node_health_crash_count());
}

/* AC: specs/09-health-factory-reset.md #1, #2 */
void test_crash_count_goes_up_after_a_panic_an_interrupt_watchdog_or_a_brownout(
    void) {
  for (size_t i = 0; i < sizeof(g_crash_reasons) / sizeof(g_crash_reasons[0]);
       i++) {
    setUp();
    node_health_port_mock_set_counter(EARLIER_CRASHES);

    boot_after(g_crash_reasons[i]);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(EARLIER_CRASHES + 1,
                                     node_health_crash_count(),
                                     node_health_reset_reason_str());
  }
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_crash_count_stays_the_same_after_a_power_on_a_restart_or_any_other_non_crash_reset(
    void) {
  for (size_t i = 0;
       i < sizeof(g_non_crash_reasons) / sizeof(g_non_crash_reasons[0]); i++) {
    setUp();
    node_health_port_mock_set_counter(EARLIER_CRASHES);

    boot_after(g_non_crash_reasons[i]);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(EARLIER_CRASHES, node_health_crash_count(),
                                     node_health_reset_reason_str());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(EARLIER_CRASHES,
                                     node_health_port_mock_counter(),
                                     node_health_reset_reason_str());
  }
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_crash_count_starts_at_zero_on_a_node_that_has_never_crashed(void) {
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);

  TEST_ASSERT_EQUAL_UINT32(0, node_health_log_mock_last_boot_crash_count());
}

/* AC: specs/09-health-factory-reset.md #1 */
void test_crash_count_is_kept_in_flash_memory_across_reboots(void) {
  boot_after(NODE_HEALTH_PORT_REASON_PANIC);
  node_health_reset();
  node_health_ao_reset();
  node_ao_port_mock_reset();

  boot_after(NODE_HEALTH_PORT_REASON_TASK_WDT);

  TEST_ASSERT_EQUAL_UINT32(2, node_health_log_mock_last_boot_crash_count());
  TEST_ASSERT_EQUAL_UINT32(2, node_health_port_mock_counter());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_crash_count_stops_at_its_maximum_instead_of_wrapping_to_zero(void) {
  node_health_port_mock_set_counter(UINT32_MAX);

  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, node_health_crash_count());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, node_health_port_mock_counter());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_crash_count_is_reported_as_zero_when_flash_memory_cannot_be_read(
    void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);
  node_health_port_mock_fail_counter_load();

  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  TEST_ASSERT_EQUAL_UINT32(0, node_health_log_mock_last_boot_crash_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_counter_failed_count());
  TEST_ASSERT_EQUAL_UINT32(EARLIER_CRASHES, node_health_port_mock_counter());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_clearing_the_crash_count_sets_it_to_zero_in_flash_memory(void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);

  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK, node_health_crash_count_clear());

  TEST_ASSERT_EQUAL_UINT32(0, node_health_crash_count());
  TEST_ASSERT_EQUAL_UINT32(0, node_health_port_mock_counter());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_crash_count_is_unchanged_when_clearing_it_cannot_be_written_to_flash_memory(
    void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);
  node_health_port_mock_fail_counter_store();

  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_ERR_IO, node_health_crash_count_clear());

  TEST_ASSERT_EQUAL_UINT32(EARLIER_CRASHES, node_health_crash_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_counter_failed_count());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_clearing_the_crash_count_is_refused_before_the_health_module_starts(
    void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);

  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_ERR_STATE, node_health_crash_count_clear());

  TEST_ASSERT_EQUAL_UINT32(EARLIER_CRASHES, node_health_port_mock_counter());
}

/* ---- Preparing flash memory ---- */

/* AC: specs/09-health-factory-reset.md #2 */
void test_flash_memory_with_no_free_pages_is_erased_and_used_again(void) {
  node_health_port_mock_set_nvs_init(NODE_HEALTH_PORT_NVS_NO_FREE,
                                     NODE_HEALTH_PORT_NVS_OK);

  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  TEST_ASSERT_EQUAL_size_t(1, node_health_port_mock_nvs_erase_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_nvs_erasing_count());
  TEST_ASSERT_EQUAL_UINT32(1, node_health_port_mock_counter());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_flash_memory_written_by_a_newer_version_is_erased_and_used_again(
    void) {
  node_health_port_mock_set_nvs_init(NODE_HEALTH_PORT_NVS_NEW_VERSION,
                                     NODE_HEALTH_PORT_NVS_OK);

  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  TEST_ASSERT_EQUAL_size_t(1, node_health_port_mock_nvs_erase_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_nvs_erasing_count());
  TEST_ASSERT_EQUAL_UINT32(1, node_health_port_mock_counter());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_crash_count_is_not_kept_when_flash_memory_cannot_be_prepared(void) {
  node_health_port_mock_set_nvs_init(NODE_HEALTH_PORT_NVS_FAILED,
                                     NODE_HEALTH_PORT_NVS_FAILED);

  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  TEST_ASSERT_EQUAL_size_t(0, node_health_port_mock_nvs_erase_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_nvs_failed_count());
  TEST_ASSERT_FALSE(node_health_port_mock_counter_is_stored());
  TEST_ASSERT_EQUAL_UINT32(0, node_health_log_mock_last_boot_crash_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_boot_count());
}

/* ---- Factory reset ---- */

/* AC: specs/09-health-factory-reset.md #3 */
void test_factory_reset_runs_the_erase_hooks_in_the_order_they_were_registered(
    void) {
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK,
                        node_health_register_reset_hook(hook_two));
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK,
                        node_health_register_reset_hook(hook_three));
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK,
                        node_health_register_reset_hook(hook_one));

  node_health_factory_reset();

  const int expected[] = {2, 3, 1};
  TEST_ASSERT_EQUAL_size_t(3, g_hook_call_count);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, g_hook_calls, 3);
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_factory_reset_runs_the_erase_hooks_before_the_config_is_erased(void) {
  store_config();
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK, node_health_register_reset_hook(
                                            hook_that_loads_the_config));

  node_health_factory_reset();

  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK, g_config_seen_by_hook);
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_factory_reset_erases_the_stored_config(void) {
  store_config();
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);

  node_health_factory_reset();

  char buf[CONFIG_BUF_LEN] = {0};
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_ERR_NOT_FOUND,
                        config_store_load(buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_size_t(1, node_health_log_mock_factory_reset_count());
}

/* AC: specs/09-health-factory-reset.md #2 */
void test_factory_reset_clears_the_crash_count(void) {
  node_health_port_mock_set_counter(EARLIER_CRASHES);
  boot_after(NODE_HEALTH_PORT_REASON_PANIC);

  node_health_factory_reset();

  TEST_ASSERT_FALSE(node_health_port_mock_counter_is_stored());
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_factory_reset_restarts_the_node(void) {
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);

  node_health_factory_reset();

  TEST_ASSERT_EQUAL_size_t(1, node_health_port_mock_restart_count());
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_factory_reset_still_restarts_the_node_when_erasing_flash_memory_fails(
    void) {
  store_config();
  boot_after(NODE_HEALTH_PORT_REASON_POWERON);
  config_store_fs_mock_set_error(CONFIG_STORE_FS_MOCK_OP_REMOVE,
                                 CONFIG_STORE_ERR_IO);
  node_health_port_mock_fail_health_erase();

  node_health_factory_reset();

  TEST_ASSERT_EQUAL_size_t(2,
                           node_health_log_mock_factory_reset_failed_count());
  TEST_ASSERT_EQUAL_size_t(1, node_health_port_mock_restart_count());
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_an_empty_erase_hook_is_rejected(void) {
  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_ERR_ARG,
                        node_health_register_reset_hook(NULL));

  node_health_factory_reset();

  TEST_ASSERT_EQUAL_size_t(1, node_health_port_mock_restart_count());
}

/* AC: specs/09-health-factory-reset.md #3 */
void test_a_fifth_erase_hook_is_rejected_because_the_table_is_full(void) {
  for (size_t i = 0; i < NODE_HEALTH_MAX_RESET_HOOKS; i++) {
    TEST_ASSERT_EQUAL_INT(NODE_HEALTH_OK,
                          node_health_register_reset_hook(hook_one));
  }

  TEST_ASSERT_EQUAL_INT(NODE_HEALTH_ERR_FULL,
                        node_health_register_reset_hook(hook_two));

  node_health_factory_reset();
  TEST_ASSERT_EQUAL_size_t(NODE_HEALTH_MAX_RESET_HOOKS, g_hook_call_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_reset_reason_is_logged_by_its_own_name_at_boot);
  RUN_TEST(test_an_unrecognised_reset_reason_is_logged_as_unknown);
  RUN_TEST(test_reset_reason_is_unknown_before_the_health_module_starts);
  RUN_TEST(
      test_boot_line_is_logged_only_once_even_if_the_health_module_is_started_twice);
  RUN_TEST(test_crash_count_goes_up_by_one_after_a_task_watchdog_reset);
  RUN_TEST(
      test_crash_count_goes_up_after_a_panic_an_interrupt_watchdog_or_a_brownout);
  RUN_TEST(
      test_crash_count_stays_the_same_after_a_power_on_a_restart_or_any_other_non_crash_reset);
  RUN_TEST(test_crash_count_starts_at_zero_on_a_node_that_has_never_crashed);
  RUN_TEST(test_crash_count_is_kept_in_flash_memory_across_reboots);
  RUN_TEST(test_crash_count_stops_at_its_maximum_instead_of_wrapping_to_zero);
  RUN_TEST(
      test_crash_count_is_reported_as_zero_when_flash_memory_cannot_be_read);
  RUN_TEST(test_clearing_the_crash_count_sets_it_to_zero_in_flash_memory);
  RUN_TEST(
      test_crash_count_is_unchanged_when_clearing_it_cannot_be_written_to_flash_memory);
  RUN_TEST(
      test_clearing_the_crash_count_is_refused_before_the_health_module_starts);
  RUN_TEST(test_flash_memory_with_no_free_pages_is_erased_and_used_again);
  RUN_TEST(
      test_flash_memory_written_by_a_newer_version_is_erased_and_used_again);
  RUN_TEST(test_crash_count_is_not_kept_when_flash_memory_cannot_be_prepared);
  RUN_TEST(
      test_factory_reset_runs_the_erase_hooks_in_the_order_they_were_registered);
  RUN_TEST(test_factory_reset_runs_the_erase_hooks_before_the_config_is_erased);
  RUN_TEST(test_factory_reset_erases_the_stored_config);
  RUN_TEST(test_factory_reset_clears_the_crash_count);
  RUN_TEST(test_factory_reset_restarts_the_node);
  RUN_TEST(
      test_factory_reset_still_restarts_the_node_when_erasing_flash_memory_fails);
  RUN_TEST(test_an_empty_erase_hook_is_rejected);
  RUN_TEST(test_a_fifth_erase_hook_is_rejected_because_the_table_is_full);
  return UNITY_END();
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void boot_after(node_health_port_reason_t reason) {
  node_health_port_mock_set_reset_reason(reason);
  node_health_init();
  TEST_ASSERT_NOT_EQUAL_INT(NODE_AO_PORT_MOCK_RUNAWAY,
                            node_ao_port_mock_run_until_idle());
}

static void record_hook(int id) {
  if (g_hook_call_count < NODE_HEALTH_MAX_RESET_HOOKS) {
    g_hook_calls[g_hook_call_count] = id;
  }
  g_hook_call_count++;
}

static void hook_one(void) { record_hook(1); }

static void hook_two(void) { record_hook(2); }

static void hook_three(void) { record_hook(3); }

static void hook_that_loads_the_config(void) {
  char buf[CONFIG_BUF_LEN] = {0};
  size_t len = 0;
  g_config_seen_by_hook = config_store_load(buf, sizeof(buf), &len);
}

static void store_config(void) {
  TEST_ASSERT_EQUAL_INT(CONFIG_STORE_OK,
                        config_store_save(g_config, sizeof(g_config) - 1));
}
