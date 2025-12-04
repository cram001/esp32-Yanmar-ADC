# Quick Test Guide

## Prerequisites

```bash
# Install PlatformIO if not already installed
pip install platformio

# Or using Homebrew (macOS)
brew install platformio
```

## Run All Tests

```bash
pio test
```

## Run Individual Test Suites

```bash
# Sender resistance tests (18 tests)
pio test -f test_sender_resistance

# ADC calibration tests (9 tests)
pio test -f test_calibrated_adc

# Engine hours tests (13 tests)
pio test -f test_engine_hours

# Engine load tests (19 tests)
pio test -f test_engine_load

# Integration tests (15 tests)
pio test -f test_integration
```

## Verbose Output

```bash
pio test -v
```

## Test on Specific Environment

```bash
pio test -e esp32-dev
```

## Expected Results

✅ **All tests should pass:**
```
Testing...
test_sender_resistance [PASSED]
test_calibrated_adc [PASSED]
test_engine_hours [PASSED]
test_engine_load [PASSED]
test_integration [PASSED]

========================================
74 Tests 0 Failures 0 Ignored
OK
========================================
```

## If Tests Fail

1. **Check the error message** - Unity provides clear failure details
2. **Review the test** - See what's being tested
3. **Check implementation** - Verify the code matches requirements
4. **Re-run specific test** - `pio test -f <test_name>`

## Common Issues

### "Command not found: pio"
```bash
# Install PlatformIO
pip install platformio
```

### "No tests found"
```bash
# Ensure you're in the project root directory
cd /path/to/esp32-yanmar-project

# Verify test files exist
ls test/
```

### "Compilation errors"
```bash
# Clean and rebuild
pio run --target clean
pio test
```

## Test Documentation

- **Full documentation**: `test/README_TESTS.md`
- **Test summary**: `TEST_SUMMARY.md`
- **Task list**: `.kiro/specs/coolant-sender-interpreter/tasks.md`

## Next Steps

1. ✅ Run tests: `pio test`
2. ✅ Verify all pass
3. ✅ Build firmware: `pio run`
4. ✅ Upload to device: `pio run --target upload`
5. ✅ Monitor output: `pio device monitor`

## Quick Commands

```bash
# Test + Build + Upload (full workflow)
pio test && pio run --target upload

# Test with verbose output
pio test -v

# Test specific suite with verbose
pio test -f test_sender_resistance -v

# Clean everything and test
pio run --target clean && pio test
```

## Status

✅ **74 tests ready to run**
- 18 sender resistance tests
- 9 ADC calibration tests  
- 13 engine hours tests
- 19 engine load tests
- 15 integration tests

Run `pio test` to validate your implementation!
