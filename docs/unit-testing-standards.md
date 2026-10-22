# Unit Testing Standards

These standards apply to every unit test in this repository:
- **Firmware host tests:** C, Unity, CMake, under `firmware/test/host/`, run with `scripts/host_test.sh`.
- **PC tools:** Python, pytest, under `tools/` (for example `tools/gateway/tests/`).

System and hardware-in-the-loop tests (`tests/system/`, spec 13) follow the naming rules below too. Their structure is defined in spec 13.

## 1. Test names are sentences

A test name must say **what behaviour is expected, in which situation**. Someone with no technical background should be able to read the test report and understand what the product does and what broke.

Write the name as a plain-English sentence in `snake_case`, prefixed with `test_`:

```
test_<subject>_<expected behaviour>[_when_<situation>]
```

Read it aloud without the `test_` prefix and the underscores. It should sound like a statement about the product.

| ✅ Good | ❌ Bad | Why the bad one is bad |
|---|---|---|
| `test_config_with_an_unknown_device_type_is_rejected` | `test_parse_err_3` | Says nothing about the behaviour. |
| `test_led_turns_on_when_the_gateway_sends_on` | `test_led_set_1` | Code detail, not behaviour. |
| `test_temperature_is_reported_in_hundredths_of_a_degree` | `test_shtc3_conv` | Abbreviated and unclear. |
| `test_node_starts_in_safe_mode_when_no_config_is_stored` | `test_boot` | Too vague to tell what failed. |
| `test_config_larger_than_4_kb_is_rejected_without_being_saved` | `test_cfg_size_check_returns_einval` | Describes the code path, not the outcome. |
| `test_motion_is_reported_immediately_when_the_sensor_detects_movement` | `test_pir_isr_cb` | Names a function, not behaviour. |

Rules:
- **Describe the outcome, not the implementation.** Prefer "is rejected", "is saved", "turns on", "is reported" over function names, return codes or variable names. Put code-level detail (`returns NODE_CFG_ERR_TYPE`) in the assertions, not in the name.
- **Use product words.** Use "sensor", "config", "LED", "gateway", "reboot", "flash memory", not `hal_i2c_xfer` or `nvs_blob`. Domain terms from `specs/README.md` (mesh, provisioning, safe mode, factory reset) are fine.
- **No abbreviations** except widely understood units (`ms`, `kb`, `c`). Write `temperature`, not `temp`. Write `config`, not `cfg`.
- **One behaviour per name.** If the name needs "and" to join two unrelated outcomes, split the test. "Is rejected and names the bad entry" is fine, because it is one outcome described fully.
- **State the situation** with `when_…` (or `with_…`, `after_…`, `if_…`) whenever the outcome depends on it.
- **Long is fine; unclear is not.** Aim for under ~100 characters. GCC and Clang have no practical identifier-length limit.
- Names must be unique across the test suite, so they can be searched for and quoted in bug reports.

### Test file and group names

- One test file per module: `firmware/test/host/test_<module>.c`, or `test_<module>.py` for Python.
- Inside a file, keep related tests together under a comment heading (C) or a test class (Python), named after the feature area in plain words:

```c
/* ---- Rejecting invalid configs ---- */
```

```python
class TestRejectingInvalidConfigs:
    def test_config_with_an_unknown_device_type_is_rejected(self): ...
```

### Stakeholder-readable reports

Unity prints the function name of every test. pytest prints the test id. Because each name is a sentence, a readable report is just the test output with `test_` removed and underscores replaced by spaces:

```
PASS  config with an unknown device type is rejected
FAIL  node starts in safe mode when no config is stored
```

## 2. Structure of a test

- **Arrange → Act → Assert**, separated by a blank line. Keep each part short.
- **One behaviour per test.** Several assertions are fine when they check the same outcome.
- **Tests are independent.** No test may rely on another test having run first. Reset all shared state, mocks and fakes in `setUp()` (Unity) or in fixtures (pytest).
- **No hidden magic numbers.** Name inputs and expected values, or explain them with a short comment:

```c
void test_temperature_is_reported_in_hundredths_of_a_degree(void)
{
    /* Raw SHTC3 reading 0x6666 is 24.999 °C by the datasheet formula, which rounds to 25.00 °C */
    hal_mock_i2c_script_read(SHTC3_ADDR, raw_reading_25_degrees, sizeof raw_reading_25_degrees);

    int16_t centi_celsius = 0;
    shtc3_read_temperature(&sensor, &centi_celsius);

    TEST_ASSERT_EQUAL_INT16(2500, centi_celsius);
}
```

- Use the **most specific assertion** available (`TEST_ASSERT_EQUAL_INT16`, `TEST_ASSERT_EQUAL_STRING`, `TEST_ASSERT_EQUAL_HEX8_ARRAY`), not `TEST_ASSERT_TRUE(a == b)`. Specific assertions give readable failure messages.
- Add a message (`TEST_ASSERT_EQUAL_INT_MESSAGE`) when the failure would otherwise be ambiguous, such as inside a loop over test cases.

## 3. What to test

- **Every acceptance criterion** in the spec that can be checked on the host has at least one test. Note the criterion in a comment above the test (`/* AC: specs/03-config-parser.md #4 */`), so the spec and the tests can be traced in both directions.
- **Behaviour through the public interface.** Test what the module's header promises, not its static helpers. If a private function seems to need its own tests, it probably belongs in its own module.
- **Error paths, not only the happy path:** invalid arguments, malformed input, bus errors from the HAL, a full or failing flash store, timeouts.
- **Boundaries:** sizes at, below and above limits (for example a 4 KB config), minimum and maximum intervals, empty lists, the largest device count.
- **Embedded-specific risks:**
  - tick-counter and timer **wrap-around** (`uint32_t` milliseconds overflow);
  - **fixed-point and unit conversions** (raw sensor value to engineering units, rounding direction);
  - **CRC and checksum** checks, including a corrupted byte;
  - **byte order** of multi-byte bus and mesh fields;
  - behaviour after **reset or reboot** (state restored from storage, counters incremented);
  - **retry and fault** handling (a sensor stops responding, then recovers).

Do not unit-test:
- vendor SDK or third-party code (ESP-IDF, cJSON, Unity, BlueZ);
- behaviour that needs real radio or hardware. That belongs in QEMU or HIL tests (spec 13). Say so in the test plan.

## 4. Mocks, fakes and time

- Code under test reaches hardware only through the HAL (spec 02). Host tests link `hal_mock` (`firmware/test/host/mocks/`), never ESP-IDF headers.
- Prefer **fakes with scriptable behaviour** (scripted I2C responses, injected GPIO edges) over strict call-by-call mocks. Tests should check outcomes, not the exact sequence of internal calls, unless that sequence is the requirement (for example a sensor's wake-up → measure → sleep protocol).
- **Time is virtual.** Advance it with the mock clock (`hal_mock_time_advance_ms`). Never `sleep()` or busy-wait in a unit test.
- **No randomness and no real I/O.** Tests must be deterministic and must not touch the network, serial ports or real files, except temporary files created by pytest's `tmp_path` fixture.
- Fakes for storage and NVS live in `mocks/` too, and are reset in `setUp()`.

## 5. Speed and hygiene

- The whole host suite should run in a few seconds. A slow unit test is a sign that it is really an integration test.
- Tests compile with the same warnings-as-errors flags as production code.
- No commented-out tests. A skipped test uses `TEST_IGNORE_MESSAGE("reason")` or `pytest.mark.skip(reason=...)`, with a reason a stakeholder can understand.
- A failing test is never "fixed" by weakening its assertion without understanding the failure. If the expected behaviour itself was wrong, update the spec first (see the `implement-embedded` skill's spec-change flow).

## 6. Running the tests

```sh
scripts/host_test.sh          # firmware host unit tests (Unity)
pytest tools                  # PC tool unit tests
```

CI runs both on every push (spec 01). A change must not be merged while they fail.

## 7. Review checklist

Before a test chunk is approved:
- [ ] Every test name reads as a clear English sentence about product behaviour.
- [ ] Every host-testable acceptance criterion has a test, with an `AC:` reference.
- [ ] Error paths and boundaries are covered.
- [ ] Tests are independent, deterministic, and use virtual time.
- [ ] Assertions are specific and their failure messages are meaningful.
- [ ] The suite passes locally. The output was shown in the review.
