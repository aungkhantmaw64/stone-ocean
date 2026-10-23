/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "node_ao.h"
#include "node_hal.h"
#include "node_hal_board.h"
#include "node_hal_selftest.h"

#if CONFIG_NODE_HAL_SELFTEST

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief I2C controller used for the bus scan. */
#define I2C_PORT 0

/** @brief Bus clock for the scan, in Hz. */
#define I2C_FREQ_HZ 100000

/** @brief Time to wait for each address to acknowledge, in ms. */
#define I2C_PROBE_TIMEOUT_MS 50

/** @brief First address scanned; 0x00–0x07 are reserved by the I2C
 *         specification. */
#define I2C_SCAN_FIRST_ADDR 0x08

/** @brief Last address scanned; 0x78–0x7F are reserved by the I2C
 *         specification. */
#define I2C_SCAN_LAST_ADDR 0x77

/** @brief 7-bit address of the SHTC3 temperature and humidity sensor. */
#define SHTC3_ADDR7 0x70

/** @brief Value of CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO meaning "do not
 *         toggle an output". */
#define NO_OUTPUT_GPIO (-1)

/** @brief Time between output toggles, in ms. */
#define TOGGLE_PERIOD_MS 500

/** @brief Events the self-test AO's queue holds. */
#define EDGE_QUEUE_LENGTH 8

/** @brief Stack size of the self-test AO's thread, in bytes. */
#define TASK_STACK_BYTES 3072

/** @brief FreeRTOS priority of the self-test AO's thread. */
#define TASK_PRIORITY 5

/** @brief First of the input-only GPIOs 34-39, which have no internal pull
 *         resistors (ESP32 datasheet); a button on one needs an external
 *         pull resistor. */
#define FIRST_INPUT_ONLY_GPIO 34

/* The status AO owns the status LED's pin; a self-test pin on it would
 * reconfigure the pin and stop the LED (spec 16). A stale sdkconfig keeps
 * old pin values, so catch that at build time. */
_Static_assert(CONFIG_NODE_HAL_SELFTEST_BUTTON_GPIO !=
                   NODE_HAL_BOARD_STATUS_LED_GPIO,
               "CONFIG_NODE_HAL_SELFTEST_BUTTON_GPIO is the status LED's pin");
_Static_assert(CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO !=
                   NODE_HAL_BOARD_STATUS_LED_GPIO,
               "CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO is the status LED's pin");
_Static_assert(CONFIG_NODE_HAL_SELFTEST_I2C_SDA !=
                   NODE_HAL_BOARD_STATUS_LED_GPIO,
               "CONFIG_NODE_HAL_SELFTEST_I2C_SDA is the status LED's pin");
_Static_assert(CONFIG_NODE_HAL_SELFTEST_I2C_SCL !=
                   NODE_HAL_BOARD_STATUS_LED_GPIO,
               "CONFIG_NODE_HAL_SELFTEST_I2C_SCL is the status LED's pin");

/* ==========================================================================
 * TYPES
 * ========================================================================== */

/** @brief Signals of the self-test AO. */
enum {
  NODE_HAL_SELFTEST_SIG_BUTTON_EDGE = NODE_AO_SIG_USER, /**< The button pin
                                                           changed level. */
  NODE_HAL_SELFTEST_SIG_TOGGLE, /**< Time to toggle the output pin. */
};

/** @brief A button edge, posted from ISR context. */
typedef struct {
  node_ao_evt_t super; /**< Signal NODE_HAL_SELFTEST_SIG_BUTTON_EDGE. */
  int pin;             /**< GPIO number of the edge. */
} node_hal_selftest_edge_evt_t;

/** @brief The self-test active object. */
typedef struct {
  node_ao_t super;           /**< Framework part; must be first. */
  node_ao_time_evt_t toggle; /**< Periodic TOGGLE_PERIOD_MS output toggle. */
  int level;                 /**< Level last written to the output pin. */
} node_hal_selftest_ao_t;

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Log tag for self-test output and name of the self-test AO. */
static const char *const TAG = "node_hal_selftest";

/** @brief The self-test AO; its state is touched only in its own thread. */
static node_hal_selftest_ao_t g_selftest = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Handle one event of the self-test AO.
 *
 * @param me The self-test AO.
 * @param e  INIT, a button edge or a toggle tick.
 */
static void node_hal_selftest_dispatch(node_ao_t *me, const node_ao_evt_t *e);

/**
 * @brief Probe every non-reserved 7-bit address on the self-test I2C bus and
 *        log which acknowledge, including whether the SHTC3 is present.
 */
static void node_hal_selftest_scan_i2c(void);

/**
 * @brief GPIO ISR callback that posts a button edge to the self-test AO.
 *
 * Runs in ISR context. An edge is dropped if the AO's queue is full.
 *
 * @param pin GPIO number of the edge.
 * @param ctx Unused.
 */
static void node_hal_selftest_on_button_edge(int pin, void *ctx);

/**
 * @brief Configure the button GPIO as an input with pull-up and watch it for
 *        edges in both directions.
 *
 * @return True on success; false after logging the failure.
 */
static bool node_hal_selftest_watch_button(void);

/**
 * @brief Configure the output GPIO for toggling, if one is set.
 *
 * @return True if the output is ready to toggle; false if none is configured
 *         or setup failed, which is logged.
 */
static bool node_hal_selftest_prepare_output(void);

/**
 * @brief Toggle the output pin; stop toggling after logging a failed write.
 *
 * @param me The self-test AO.
 */
static void node_hal_selftest_toggle_output(node_hal_selftest_ao_t *me);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

void node_hal_selftest_start(void) {
  node_ao_err_t err = node_ao_time_evt_init(
      &g_selftest.toggle, &g_selftest.super, NODE_HAL_SELFTEST_SIG_TOGGLE);
  if (err == NODE_AO_ERR_STATE) {
    ESP_LOGW(TAG, "self-test already started");
    return;
  }
  const node_ao_config_t cfg = {
      .name = TAG,
      .priority = TASK_PRIORITY,
      .stack_bytes = TASK_STACK_BYTES,
      .queue_len = EDGE_QUEUE_LENGTH,
      .evt_size = sizeof(node_hal_selftest_edge_evt_t),
  };
  if (err == NODE_AO_OK) {
    err = node_ao_start(&g_selftest.super, node_hal_selftest_dispatch, &cfg);
  }
  if (err != NODE_AO_OK) {
    ESP_LOGE(TAG, "could not start the self-test: %d", err);
  }
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void node_hal_selftest_dispatch(node_ao_t *me, const node_ao_evt_t *e) {
  node_hal_selftest_ao_t *self = (node_hal_selftest_ao_t *)me;
  switch (e->sig) {
    case NODE_AO_SIG_INIT:
      node_hal_selftest_scan_i2c();
      /* A failed button setup is already logged; the output toggle still
       * runs. */
      (void)node_hal_selftest_watch_button();
      if (node_hal_selftest_prepare_output()) {
        (void)node_ao_time_evt_arm(&self->toggle, TOGGLE_PERIOD_MS,
                                   TOGGLE_PERIOD_MS);
      }
      break;
    case NODE_HAL_SELFTEST_SIG_BUTTON_EDGE: {
      const node_hal_selftest_edge_evt_t *edge =
          (const node_hal_selftest_edge_evt_t *)e;
      ESP_LOGI(TAG, "edge on GPIO %d at %lu ms, level now %d", edge->pin,
               (unsigned long)node_hal_time_ms(),
               node_hal_gpio_read(edge->pin));
      break;
    }
    case NODE_HAL_SELFTEST_SIG_TOGGLE:
      node_hal_selftest_toggle_output(self);
      break;
    default:
      break;
  }
}

static void node_hal_selftest_scan_i2c(void) {
  node_hal_i2c_bus_t bus = NULL;
  node_hal_err_t err = node_hal_i2c_bus_init(
      I2C_PORT, CONFIG_NODE_HAL_SELFTEST_I2C_SDA,
      CONFIG_NODE_HAL_SELFTEST_I2C_SCL, I2C_FREQ_HZ, &bus);
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "I2C bus init on SDA %d / SCL %d failed: %d",
             CONFIG_NODE_HAL_SELFTEST_I2C_SDA, CONFIG_NODE_HAL_SELFTEST_I2C_SCL,
             err);
    return;
  }

  ESP_LOGI(TAG, "I2C scan 0x%02X-0x%02X on SDA %d / SCL %d",
           I2C_SCAN_FIRST_ADDR, I2C_SCAN_LAST_ADDR,
           CONFIG_NODE_HAL_SELFTEST_I2C_SDA, CONFIG_NODE_HAL_SELFTEST_I2C_SCL);
  int found = 0;
  bool shtc3_found = false;
  for (uint8_t addr = I2C_SCAN_FIRST_ADDR; addr <= I2C_SCAN_LAST_ADDR; addr++) {
    err = node_hal_i2c_probe(bus, addr, I2C_PROBE_TIMEOUT_MS);
    if (err == NODE_HAL_OK) {
      ESP_LOGI(TAG, "  0x%02X acknowledged", addr);
      found++;
      shtc3_found |= addr == SHTC3_ADDR7;
    } else if (err != NODE_HAL_ERR_NACK) {
      ESP_LOGW(TAG, "  0x%02X probe failed: %d", addr, err);
    }
  }
  ESP_LOGI(TAG, "I2C scan done: %d device(s); SHTC3 at 0x%02X %s", found,
           SHTC3_ADDR7, shtc3_found ? "found" : "NOT found");
}

static void node_hal_selftest_on_button_edge(int pin, void *ctx) {
  (void)ctx;
  const node_hal_selftest_edge_evt_t edge = {
      .super = {.sig = NODE_HAL_SELFTEST_SIG_BUTTON_EDGE},
      .pin = pin,
  };
  /* An ISR cannot log, so an edge that finds the queue full is dropped. */
  (void)node_ao_post_from_isr(&g_selftest.super, &edge, sizeof(edge));
}

static bool node_hal_selftest_watch_button(void) {
  const int pin = CONFIG_NODE_HAL_SELFTEST_BUTTON_GPIO;
  const node_hal_gpio_pull_t pull = (pin >= FIRST_INPUT_ONLY_GPIO)
                                        ? NODE_HAL_GPIO_PULL_NONE
                                        : NODE_HAL_GPIO_PULL_UP;
  node_hal_err_t err = node_hal_gpio_config(pin, NODE_HAL_GPIO_IN, pull);
  if (err == NODE_HAL_OK) {
    err = node_hal_gpio_set_isr(pin, NODE_HAL_GPIO_EDGE_ANY,
                                node_hal_selftest_on_button_edge, NULL);
  }
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "button GPIO %d setup failed: %d", pin, err);
    return false;
  }
  ESP_LOGI(TAG, "logging edges on button GPIO %d (level now %d)", pin,
           node_hal_gpio_read(pin));
  return true;
}

static bool node_hal_selftest_prepare_output(void) {
  const int pin = CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO;
  if (pin == NO_OUTPUT_GPIO) {
    ESP_LOGI(TAG, "no output GPIO configured; not toggling");
    return false;
  }
  node_hal_err_t err =
      node_hal_gpio_config(pin, NODE_HAL_GPIO_OUT, NODE_HAL_GPIO_PULL_NONE);
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "output GPIO %d setup failed: %d", pin, err);
    return false;
  }
  ESP_LOGI(TAG, "toggling output GPIO %d every %d ms", pin, TOGGLE_PERIOD_MS);
  return true;
}

static void node_hal_selftest_toggle_output(node_hal_selftest_ao_t *me) {
  me->level = !me->level;
  node_hal_err_t err =
      node_hal_gpio_write(CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO, me->level);
  if (err != NODE_HAL_OK) {
    ESP_LOGE(TAG, "output GPIO %d write failed: %d; stopped toggling",
             CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO, err);
    (void)node_ao_time_evt_disarm(&me->toggle);
  }
}

#else

void node_hal_selftest_start(void) {}

#endif
