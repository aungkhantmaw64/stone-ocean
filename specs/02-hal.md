# 02 — HAL abstraction

**Status:** in-progress
**Depends on:** 01
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/node_hal`

## Goal
Provide a thin hardware abstraction for GPIO, I2C and time so drivers and logic can be unit-tested on the host with a mock. Without this seam, everything above would only be testable on hardware.

## Context
The component is named `node_hal`, not `hal`. ESP-IDF has a built-in `hal` component, and a project component with the same name replaces it and breaks the build. For the same reason, public headers must not be placed under a `hal/` include directory. The `hal_` function prefix is fine.

Drivers (specs 07, 08) and the health module (09) use only this HAL, never ESP-IDF driver headers directly. Two implementations exist:
- `hal_esp`: ESP-IDF implementation, using the `driver/gpio.h` and `driver/i2c_master.h` new I2C master API (IDF ≥ 5.2).
- `hal_mock`: host implementation in `firmware/test/host/mocks/`, with scripted I2C responses and controllable time and GPIO levels.

## Hardware details
Classic ESP32 GPIO notes, for implementers. Confirm against the ESP32 datasheet:
- GPIO 6–11 are connected to the SPI flash and must not be used.
- GPIO 34–39 are input-only and have no internal pull-ups/pull-downs.
- GPIO 0, 2, 5, 12, 15 are strapping pins. GPIO 0 is the DevKitC BOOT button.

## Scope
- In scope:
  - GPIO configure, read, write, and edge interrupt with a callback.
  - I2C bus init and device add, write, write-then-read with timeout.
  - Monotonic time in ms, and a delay.
  - Mock implementation with call recording and scripted responses.
- Out of scope: ADC and PWM (later), SPI.

## Interface
```c
typedef enum { HAL_OK = 0, HAL_ERR_ARG, HAL_ERR_TIMEOUT, HAL_ERR_NACK, HAL_ERR_IO } hal_err_t;

typedef enum { HAL_GPIO_IN, HAL_GPIO_OUT } hal_gpio_dir_t;
typedef enum { HAL_PULL_NONE, HAL_PULL_UP, HAL_PULL_DOWN } hal_pull_t;
typedef enum { HAL_EDGE_RISING, HAL_EDGE_FALLING, HAL_EDGE_ANY } hal_edge_t;
typedef void (*hal_gpio_isr_cb)(int pin, void *ctx);   /* called from ISR context */

hal_err_t hal_gpio_config(int pin, hal_gpio_dir_t dir, hal_pull_t pull);
hal_err_t hal_gpio_write(int pin, int level);
int       hal_gpio_read(int pin);
hal_err_t hal_gpio_set_isr(int pin, hal_edge_t edge, hal_gpio_isr_cb cb, void *ctx);

typedef struct hal_i2c_bus *hal_i2c_bus_t;
typedef struct hal_i2c_dev *hal_i2c_dev_t;
hal_err_t hal_i2c_bus_init(int port, int sda, int scl, uint32_t freq_hz, hal_i2c_bus_t *out);
hal_err_t hal_i2c_dev_add(hal_i2c_bus_t bus, uint8_t addr7, hal_i2c_dev_t *out);
hal_err_t hal_i2c_write(hal_i2c_dev_t dev, const uint8_t *buf, size_t len, uint32_t timeout_ms);
hal_err_t hal_i2c_write_read(hal_i2c_dev_t dev, const uint8_t *w, size_t wlen,
                             uint8_t *r, size_t rlen, uint32_t timeout_ms);

uint32_t hal_time_ms(void);
void     hal_delay_ms(uint32_t ms);
```
Mock-only helpers (host): `mock_gpio_set_input(pin, level)` triggers an ISR on a matching edge, `mock_i2c_expect_write(...)`, `mock_i2c_queue_read(...)`, `mock_time_advance(ms)`.

## Behavior & constraints
- All functions validate arguments and return `HAL_ERR_ARG` rather than asserting.
- `hal_gpio_set_isr` installs the ESP-IDF GPIO ISR service once, on first use.
- ISR callbacks must be safe to call from ISR context. Document that callers only signal a task or queue from them.
- No dynamic allocation after init.

## Verification
- **Simulation/host:** Unity tests for the mock itself (scripted I2C sequence round-trip, edge-triggered callbacks, time advance).
- **On hardware:** a temporary test command or example: toggle an output pin, read the BOOT button (GPIO0) and log edges. Run an I2C scan across addresses 0x08–0x77 and log the addresses that ACK. The SHTC3 should appear when wired.

## Acceptance criteria
- [ ] Public header compiles in both the ESP-IDF and host builds with no ESP-IDF includes.
- [ ] ESP implementation uses the IDF I2C master driver and the GPIO ISR service.
- [ ] Mock supports scripted I2C transactions, GPIO edge injection and virtual time, with unit tests.
- [ ] Invalid arguments return `HAL_ERR_ARG` (tested on host).
- [ ] On hardware, an I2C scan finds the wired SHTC3, and BOOT-button edges are logged.

## Open questions
- Should GPIO pin validity (flash pins, input-only pins) be checked here or only in config validation (spec 03)? Proposed: in both places, as a defensive check in the HAL.
