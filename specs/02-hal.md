# 02 — HAL abstraction

**Status:** done
**Depends on:** 01
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/node_hal`

## Goal
Provide a thin hardware abstraction for GPIO, I2C and time so drivers and logic can be unit-tested on the host with a mock. Without this seam, everything above would only be testable on hardware.

## Context
The component is named `node_hal`, not `hal`. ESP-IDF has a built-in `hal` component, and a project component with the same name replaces it and breaks the build. For the same reason, public headers must not be placed under a `hal/` include directory. The public header is `node_hal.h`.

Naming follows the project rule: source files are named `node_hal_<area>.c`, every function in a file is prefixed with that file's name, and public types, enum values and macros take the `node_hal_` / `NODE_HAL_` prefix.

Drivers (specs 07, 08) and the health module (09) use only this HAL, never ESP-IDF driver headers directly. Two implementations exist:
- ESP-IDF implementation in `firmware/components/node_hal/` (`node_hal_gpio.c`, `node_hal_i2c.c`, `node_hal_time.c`), using `driver/gpio.h` and the `driver/i2c_master.h` new I2C master API (IDF ≥ 5.2).
- Host mock in `firmware/test/host/mocks/`, with files of the same names plus `node_hal_mock.c`, offering scripted I2C responses and controllable time and GPIO levels.

## Hardware details
Classic ESP32 GPIO notes, for implementers. Confirm against the ESP32 datasheet:
- GPIO 6–11 are connected to the SPI flash and must not be used.
- GPIO 34–39 are input-only and have no internal pull-ups/pull-downs.
- GPIO 0, 2, 5, 12, 15 are strapping pins. GPIO 0 is the DevKitC BOOT button.
- On the WROVER module, PSRAM occupies GPIO 16 and 17.

The HAL does not hard-code these lists. It relies on ESP-IDF to reject unusable pins: `gpio_config` rejects pins that do not exist and output on input-only pins; `gpio_pullup_en`/`gpio_pulldown_en` reject pulls on input-only pins; `esp_gpio_is_reserved()` reports pins claimed at runtime by flash, PSRAM or another driver (such as an I2C bus). `esp_gpio_is_reserved()` is private ESP-IDF API, so re-check it when moving off the pinned IDF version.

## Scope
- In scope:
  - GPIO configure, read, write, and edge interrupt with a callback.
  - I2C bus init and device add, write, write-then-read with timeout, and probing an address without adding a device.
  - Monotonic time in ms, and a delay.
  - Mock implementation with call recording and scripted responses.
- Out of scope: ADC and PWM (later), SPI.

## Interface
```c
typedef enum { NODE_HAL_OK = 0, NODE_HAL_ERR_ARG, NODE_HAL_ERR_TIMEOUT, NODE_HAL_ERR_NACK, NODE_HAL_ERR_IO } node_hal_err_t;

/* node_hal_gpio.c */
typedef enum { NODE_HAL_GPIO_IN, NODE_HAL_GPIO_OUT } node_hal_gpio_dir_t;
typedef enum { NODE_HAL_GPIO_PULL_NONE, NODE_HAL_GPIO_PULL_UP, NODE_HAL_GPIO_PULL_DOWN } node_hal_gpio_pull_t;
typedef enum { NODE_HAL_GPIO_EDGE_RISING, NODE_HAL_GPIO_EDGE_FALLING, NODE_HAL_GPIO_EDGE_ANY } node_hal_gpio_edge_t;
typedef void (*node_hal_gpio_isr_cb)(int pin, void *ctx);   /* called from ISR context */

node_hal_err_t node_hal_gpio_config(int pin, node_hal_gpio_dir_t dir, node_hal_gpio_pull_t pull);
node_hal_err_t node_hal_gpio_write(int pin, int level);
int            node_hal_gpio_read(int pin);                 /* -1 for an invalid pin */
node_hal_err_t node_hal_gpio_set_isr(int pin, node_hal_gpio_edge_t edge, node_hal_gpio_isr_cb cb, void *ctx);

/* node_hal_i2c.c */
typedef struct node_hal_i2c_bus *node_hal_i2c_bus_t;
typedef struct node_hal_i2c_dev *node_hal_i2c_dev_t;
node_hal_err_t node_hal_i2c_bus_init(int port, int sda, int scl, uint32_t freq_hz, node_hal_i2c_bus_t *out);
node_hal_err_t node_hal_i2c_dev_add(node_hal_i2c_bus_t bus, uint8_t addr7, node_hal_i2c_dev_t *out);
node_hal_err_t node_hal_i2c_probe(node_hal_i2c_bus_t bus, uint8_t addr7, uint32_t timeout_ms); /* OK on ACK, ERR_NACK if absent */
node_hal_err_t node_hal_i2c_write(node_hal_i2c_dev_t dev, const uint8_t *buf, size_t len, uint32_t timeout_ms);
node_hal_err_t node_hal_i2c_write_read(node_hal_i2c_dev_t dev, const uint8_t *w, size_t wlen,
                                       uint8_t *r, size_t rlen, uint32_t timeout_ms);

/* node_hal_time.c */
uint32_t node_hal_time_ms(void);
void     node_hal_time_delay_ms(uint32_t ms);
```
Mock-only helpers (host), declared in `node_hal_mock.h`:
- `node_hal_mock_reset()` clears all mock state.
- `node_hal_gpio_mock_set_input(pin, level)` sets an input level and triggers the ISR on a matching edge.
- `node_hal_i2c_mock_expect_write(...)`, `node_hal_i2c_mock_queue_read(...)`, `node_hal_i2c_mock_queue_error(...)` and `node_hal_i2c_mock_set_present(...)` script I2C traffic. Steps are consumed in order; a transfer that does not match returns `NODE_HAL_ERR_IO`.
- `node_hal_i2c_mock_failure()` describes the first mismatching transfer (NULL if none) and `node_hal_i2c_mock_pending_steps()` counts unconsumed steps, so tests can check the whole script ran.
- `node_hal_gpio_mock_reset()`, `node_hal_i2c_mock_reset()` and `node_hal_time_mock_reset()` reset one area each.
- `node_hal_time_mock_advance(ms)` and `node_hal_time_mock_set(ms)` control virtual time.

## Behavior & constraints
- All functions validate arguments and return `NODE_HAL_ERR_ARG` rather than asserting.
- GPIO functions return `NODE_HAL_ERR_ARG` for pins the hardware cannot use, as reported by ESP-IDF (see Hardware details). Config validation (spec 03) also checks pins, to give readable errors.
- `node_hal_gpio_set_isr` installs the ESP-IDF GPIO ISR service once, on first use.
- ISR callbacks must be safe to call from ISR context. Document that callers only signal a task or queue from them.
- No dynamic allocation after init.
- `NODE_HAL_ERR_NACK` means the device did not acknowledge its address. In IDF 5.4.4 the I2C master driver reports a NACK and a hardware bus timeout with the same error, so after a failed transfer the ESP implementation probes the address to tell them apart.

## Verification
- **Simulation/host:** Unity tests for the mock itself (scripted I2C sequence round-trip, edge-triggered callbacks, time advance).
- **On hardware:** a temporary test command or example: toggle an output pin, read the BOOT button (GPIO0) and log edges. Run an I2C scan across addresses 0x08–0x77 and log the addresses that ACK. The SHTC3 should appear when wired. Enable it with `CONFIG_NODE_HAL_SELFTEST=y` (`idf.py menuconfig` → Node HAL); the SHTC3 is wired to SDA GPIO 32 / SCL GPIO 33 at address 0x70, and the output pin is set with `CONFIG_NODE_HAL_SELFTEST_OUTPUT_GPIO`.

## Acceptance criteria
- [x] Public header compiles in both the ESP-IDF and host builds with no ESP-IDF includes.
- [x] ESP implementation uses the IDF I2C master driver and the GPIO ISR service.
- [x] Mock supports scripted I2C transactions, GPIO edge injection and virtual time, with unit tests.
- [x] Invalid arguments return `NODE_HAL_ERR_ARG` (tested on host).
- [x] On hardware, an I2C scan finds the wired SHTC3, and BOOT-button edges are logged.

Verified on hardware (WROVER board, commit e2fe47b): the self-test scan found the SHTC3 at 0x70, BOOT-button presses and releases were logged with contact bounce visible, and 8 MB PSRAM was detected (4 MB mapped).

Not yet exercised on hardware: an I2C transfer that fails after the address was acknowledged (reported as `NODE_HAL_ERR_IO`). QEMU covered pin rejection and the probe's ACK/NACK paths.

## Open questions
- None. Resolved: GPIO pin validity is checked in both the HAL (through ESP-IDF errors and reservations) and config validation (spec 03).
