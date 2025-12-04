# Test Suite Documentation

## Overview

This test suite validates the Yanmar 3JH3E engine monitoring system using PlatformIO's native testing framework with Unity.

## Test Structure

```
test/
├── test_sender_resistance/     # Coolant sender resistance calculation tests
├── test_calibrated_adc/         # ADC calibration and voltage tests
├── test_engine_hours/           # Engine hours accumulation tests
├── test_engine_load/            # Engine load calculation tests
└── README_TESTS.md              # This file
```

## Running Tests

### Run All Tests
```bash
pio test
```

### Run Specific Test
```bash
pio test -f test_sender_resistance
pio test -f test_calibrated_adc
pio test -f test_engine_hours
pio test -f test_engine_load
```

### Run Tests on Specific Environment
```bash
pio test -e esp32-dev
```

### Run Tests with Verbose Output
```bash
pio test -v
```

## Test Coverage

### 1. Sender Resistance Tests (18 tests)
**File:** `test_sender_resistance/test_sender_resistance.cpp`

**Coverage:**
- ✅ Input validation (NaN, infinite, low voltage, impossible voltage)
- ✅ Voltage divider compensation (5:1 ratio)
- ✅ Series circuit calculation (hot, cold, mid-range)
- ✅ Resistance range validation (20Ω - 20kΩ)
- ✅ Supply voltage independence (12V, 14V)
- ✅ Edge cases (zero, negative voltage)

**Key Tests:**
- `test_sender_resistance_rejects_nan_input`
- `test_voltage_divider_compensation_correctness`
- `test_series_circuit_hot_engine` (29.6Ω)
- `test_series_circuit_cold_engine` (1352Ω)
- `test_supply_voltage_independence_12v`
- `test_supply_voltage_independence_14v`

### 2. Calibrated ADC Tests (9 tests)
**File:** `test_calibrated_adc/test_calibrated_adc.cpp`

**Coverage:**
- ✅ ADC configuration (pin mapping, attenuation, width)
- ✅ Voltage calibration (2.5V reference)
- ✅ Sampling rate calculations
- ✅ Voltage range validation (0V - 2.5V)

**Note:** These are structural tests. Full hardware testing requires the ESP32 device.

### 3. Engine Hours Tests (13 tests)
**File:** `test_engine_hours/test_engine_hours.cpp`

**Coverage:**
- ✅ Hour accumulation logic (above/below 500 RPM threshold)
- ✅ Time conversions (ms → hours → seconds)
- ✅ Rounding logic (to 0.1 hour precision)
- ✅ RPM threshold behavior (exactly 500, 501)
- ✅ Edge cases (zero, negative, very high RPM)

**Key Tests:**
- `test_hours_accumulate_when_rpm_above_threshold`
- `test_hours_do_not_accumulate_when_rpm_below_threshold`
- `test_rpm_threshold_exactly_500`
- `test_hours_to_seconds_conversion`

### 4. Engine Load Tests (19 tests)
**File:** `test_engine_load/test_engine_load.cpp`

**Coverage:**
- ✅ Power calculation from fuel rate (BSFC-based)
- ✅ Load fraction calculation (0-1 range)
- ✅ BSFC and fuel density constants
- ✅ Unit conversions (L → kg → g)
- ✅ Yanmar 3JH3E specific values
- ✅ Edge cases (zero, negative, NaN)

**Key Tests:**
- `test_power_calculation_typical_fuel_rate`
- `test_load_calculation_50_percent`
- `test_max_power_at_3800_rpm`
- `test_typical_cruise_load`

## Test Results Interpretation

### Success Output
```
test/test_sender_resistance/test_sender_resistance.cpp:123:test_sender_resistance_rejects_nan_input [PASSED]
...
-----------------------
18 Tests 0 Failures 0 Ignored
OK
```

### Failure Output
```
test/test_sender_resistance/test_sender_resistance.cpp:45:test_voltage_divider_compensation_correctness:FAIL: Expected 268.18 Was 270.5
...
-----------------------
18 Tests 1 Failures 0 Ignored
FAIL
```

## Adding New Tests

### 1. Create Test Directory
```bash
mkdir test/test_new_feature
```

### 2. Create Test File
```cpp
#include <unity.h>

void setUp(void) {
    // Setup code
}

void tearDown(void) {
    // Cleanup code
}

void test_new_feature(void) {
    TEST_ASSERT_EQUAL(expected, actual);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_new_feature);
    UNITY_END();
}

void loop() {}
```

### 3. Run New Test
```bash
pio test -f test_new_feature
```

## Unity Assertions Reference

### Numeric Assertions
```cpp
TEST_ASSERT_EQUAL(expected, actual)
TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)
TEST_ASSERT_GREATER_THAN(threshold, actual)
TEST_ASSERT_LESS_THAN(threshold, actual)
TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual)
TEST_ASSERT_LESS_OR_EQUAL(threshold, actual)
```

### Boolean Assertions
```cpp
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
```

### Special Value Assertions
```cpp
TEST_ASSERT_TRUE(std::isnan(value))
TEST_ASSERT_TRUE(std::isinf(value))
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)
```

## Continuous Integration

To run tests in CI/CD pipeline:

```yaml
# .github/workflows/test.yml
name: PlatformIO Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - uses: actions/setup-python@v2
      - run: pip install platformio
      - run: pio test
```

## Test Maintenance

### When to Update Tests

1. **Code changes** - Update tests when modifying implementation
2. **New features** - Add tests for new functionality
3. **Bug fixes** - Add regression tests
4. **Calibration changes** - Update expected values

### Test Quality Guidelines

- ✅ Each test should test ONE thing
- ✅ Use descriptive test names
- ✅ Include edge cases and error conditions
- ✅ Test both valid and invalid inputs
- ✅ Keep tests independent (no shared state)
- ✅ Use appropriate assertion tolerances for floats

## Known Limitations

1. **Hardware-dependent tests** - ADC tests require actual ESP32 hardware
2. **Timing-dependent tests** - Engine hours tests use mock time
3. **Integration tests** - Full system tests require SignalK server

## Future Test Additions

- [ ] Temperature curve interpolation tests
- [ ] Median filter spike rejection tests
- [ ] Moving average smoothing tests
- [ ] SignalK output format tests
- [ ] Configuration persistence tests
- [ ] RPM frequency calculation tests
- [ ] Fuel flow curve interpolation tests

## Troubleshooting

### Tests Won't Compile
- Check that all header files are accessible
- Verify platformio.ini test configuration
- Ensure Unity framework is available

### Tests Fail on Hardware
- Verify correct board is selected
- Check serial monitor baud rate (115200)
- Ensure hardware is properly connected

### Timeout Errors
- Increase test timeout in platformio.ini
- Check for infinite loops in test code
- Verify serial communication is working

## Contact

For test-related questions, see the main README.md or check the task list in `.kiro/specs/coolant-sender-interpreter/tasks.md`.
