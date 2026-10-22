# 08 — SHTC3 driver

**Status:** todo
**Depends on:** 06
**Target:** ESP32-DevKitC · ESP-IDF v5.x · `firmware/components/drivers/shtc3.c`

## Goal
Implement the Sensirion SHTC3 temperature/humidity driver as a `dev_driver_t` plugin. It produces periodic `temp_c` / `rh_pct` readings that the framework (06) throttles into reports.

## Context
- Config entry type `shtc3`, on a configured I2C bus.
- The framework calls `sample` every `sample_interval_ms`.
- The mesh layer (11) exposes the readings as Sensor Server properties.

## Hardware details
**All values below must be confirmed against the Sensirion SHTC3 datasheet before implementing.**
- I2C 7-bit address **0x70** (fixed). Supports standard (100 kHz) and fast mode (400 kHz).
- Supply 1.62–3.6 V. Power it from the DevKitC 3V3 pin. Pull-ups on SDA/SCL are required, either on the breakout or external.
- 16-bit commands, sent MSB first:
  - Wakeup `0x3517`
  - Sleep `0xB098`
  - Soft reset `0x805D`
  - Read ID `0xEFC8`
  - Measure, normal mode, T first, clock stretching disabled: `0x7866`
- Each 16-bit data word is followed by a CRC-8 (polynomial 0x31, init 0xFF).
- Conversion: `T[°C] = -45 + 175 * S_T / 65536`, `RH[%] = 100 * S_RH / 65536`.
- Timing: wake-up time and max measurement time are in the datasheet. Poll (with no clock stretching) or wait the datasheet's max measurement time before reading.

## Scope
- In scope:
  - Init: soft reset, read ID and verify it (log the ID).
  - Sample: wakeup → measure → wait → read 6 bytes → verify CRCs → convert → sleep.
  - CRC-8 helper.
- Out of scope: low-power measurement mode, clock-stretching mode.

## Interface
```c
extern const dev_driver_t shtc3_driver;   /* type NODE_CONFIG_DEV_SHTC3 */
void shtc3_driver_register(void);

/* pure helpers, exposed for host tests */
uint8_t shtc3_crc8(const uint8_t *data, size_t len);
float   shtc3_raw_to_temp_c(uint16_t raw);
float   shtc3_raw_to_rh_pct(uint16_t raw);
```

## Behavior & constraints
- A CRC mismatch or I2C error makes `sample` return an error, and the framework's fault logic (06) applies. Never report a value with a bad CRC.
- The sensor is put to sleep after every measurement.
- Clamp RH to 0–100 %.
- `sample` must finish within 50 ms, including the measurement wait.

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
- [ ] CRC-8 and conversion functions pass known-answer tests from the datasheet.
- [ ] The sample transaction matches the datasheet command sequence (host test with the scripted mock).
- [ ] Bad CRC and I2C errors never produce a reading.
- [ ] Driver registers and instantiates from config. The ID is read and logged at init.
- [ ] On hardware, plausible readings appear and the fault/recovery path works.

## Open questions
- Verify all command codes, timings and the CRC test vector against the current SHTC3 datasheet revision.
