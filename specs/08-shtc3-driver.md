# 08 — SHTC3 driver

**Status:** done
**Depends on:** 06
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/node_dev/node_dev_shtc3.c`

## Goal
Implement the Sensirion SHTC3 temperature/humidity driver as a `node_dev_driver_t` plugin. It produces periodic `temp_c` / `rh_pct` readings that the framework (06) throttles into reports.

## Context
- Config entry type `shtc3`, on a configured I2C bus.
- The framework calls `sample` every `sample_interval_ms`.
- The mesh layer (11) exposes the readings as Sensor Server properties.

## Hardware details
Values below are confirmed against the Sensirion SHTC3 datasheet.
- I2C 7-bit address **0x70** (fixed). Supports standard (100 kHz) and fast mode (400 kHz).
- Supply 1.62–3.6 V. Power it from the DevKitC 3V3 pin. Pull-ups on SDA/SCL are required, either on the breakout or external.
- 16-bit commands, sent MSB first:
  - Wakeup `0x3517`
  - Sleep `0xB098`
  - Soft reset `0x805D`
  - Read ID `0xEFC8`
  - Measure, normal mode, T first, clock stretching disabled: `0x7866`
- Each 16-bit data word is followed by a CRC-8 (polynomial 0x31, init 0xFF). Datasheet example: CRC of `0xBE 0xEF` is `0x92`.
- A measurement with `0x7866` returns 6 bytes: T MSB, T LSB, CRC, RH MSB, RH LSB, CRC.
- Read ID returns 3 bytes: the ID word, then its CRC. Bits 11 and 5:0 hold the product code: `(id & 0x083F) == 0x0807` for an SHTC3.
- Conversion: `T[°C] = -45 + 175 * S_T / 65536`, `RH[%] = 100 * S_RH / 65536`.
- Timing: wake-up time max 240 µs, soft reset time max 240 µs, measurement time in normal mode max 12.1 ms. The driver busy-waits the 240 µs times with `node_hal_time_busy_wait_us()` (spec 02) and waits 13 ms (12.1 ms rounded up) before reading a measurement.
- While asleep, the sensor accepts only the Wakeup command.

## Scope
- In scope:
  - Init: wakeup → wait → soft reset → wait → read ID, check its CRC and product code, log it → sleep. The I2C device is added once per device index and reused by init retries, since the HAL cannot remove a device.
  - Sample, in two non-blocking steps: wakeup → wait → measure, then return `NODE_DEV_PENDING` with `again_ms` = 13; on the next call read 6 bytes → verify CRCs → convert → sleep.
  - CRC-8 helper.
- Out of scope: low-power measurement mode, clock-stretching mode.

## Interface
```c
node_dev_err_t node_dev_shtc3_register(void);   /* registers the driver (NODE_CONFIG_DEV_SHTC3, sample_sig READING) */

/* pure helpers, exposed for host tests */
uint8_t node_dev_shtc3_crc8(const uint8_t *data, size_t len);
float   node_dev_shtc3_raw_to_temp_c(uint16_t raw);
float   node_dev_shtc3_raw_to_rh_pct(uint16_t raw);
```

## Behavior & constraints
- A CRC mismatch or I2C error makes `sample` return an error, and the framework's fault logic (06) applies. Never report a value with a bad CRC.
- The sensor is put to sleep after every measurement.
- Clamp RH to 0–100 %.
- `sample` never waits for the measurement: it returns `NODE_DEV_PENDING` and the framework calls it again (spec 06). Each call finishes within a few bus transfers.

## Verification
- **Simulation/host:** Unity tests with the scripted mock I2C cover:
  - the CRC-8 known-answer test from the datasheet example (confirm the vector there)
  - conversion at boundary raw values (0, 0xFFFF)
  - the full sample transaction sequence (exact write bytes)
  - CRC-failure handling
  - an init failure on NACK
- **On hardware:**
  - With the example config, readings appear in the log every `sample_interval_ms`, and room temperature and humidity look plausible.
  - Breathing on the sensor raises RH, and a report is emitted (delta threshold).
  - Unplugging it leads to a fault after 3 samples. Replugging recovers it within 30 s.

## Acceptance criteria
- [x] CRC-8 and conversion functions pass known-answer tests from the datasheet.
- [x] The sample transaction matches the datasheet command sequence (host test with the scripted mock).
- [x] Bad CRC and I2C errors never produce a reading.
- [x] Driver registers and instantiates from config. The ID is read and logged at init.
- [x] On hardware, plausible readings appear and the fault/recovery path works.

Host tests: `test_node_dev_shtc3` (26 tests) runs the real driver on the framework, the mock HAL and the host AO port, and passes under ASan, LSan and UBSan (commit b0b8b61). Every bus test checks that the scripted transfers ran exactly and in full. Mutation checks confirmed the tests catch a wrong CRC initial value or polynomial, a wrong conversion span or full scale, a missing soft-reset wait, a skipped sleep at init or after a failed read, a missing product-code check, an init retry that adds the I2C device again, a measurement read after 12 ms, a stale measuring step, an ignored failed sleep, an unchecked humidity CRC and humidity taken from the temperature word.

On the custom WROVER board (SHTC3 on I2C port 0, SDA 32 / SCL 33, 100 kHz), the log showed `SHTC3 id 0x0887` at init, readings of about 27.1 °C and 54.5 % RH, and a report when RH rose by more than 2 %. Unplugging the sensor marked it FAULT after 3 failed samples, and replugging it recovered it.

## Open questions
- None. Resolved: command codes, timings, CRC test vector and the ID product code were confirmed against the datasheet.
