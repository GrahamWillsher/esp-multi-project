# ESP32 Common - Unit Tests

This directory contains unit tests for the ESP32 Common library utilities.

## Test Framework

Tests use the [Unity](https://github.com/ThrowTheSwitch/Unity) test framework, which is compatible with PlatformIO's native testing features.

## Running Tests

### PlatformIO Native Testing

For components that don't require ESP32 hardware (like version utilities):

```bash
cd /path/to/esp32common
pio test -e native
```

### ESP32 Hardware Testing

For tests that require ESP32 hardware or peripherals:

```bash
pio test -e esp32
```

## Test Files

### version_utils_test.cpp

Unit tests for firmware version comparison and compatibility checking.

**Coverage:**
- Basic version comparison (monotonic increment)
- Large version gaps (non-wraparound)
- uint32_t wraparound detection (4294967295 → 0)
- Edge cases at wraparound boundary (2^31)
- Sequential increments across wraparound
- Backwards time travel detection
- Range-based compatibility checking
- Practical use cases (receiver ↔ transmitter compatibility)
- Zero version handling

**Expected Results:**
- All tests should pass ✓
- ~15 test cases covering edge cases and normal operation
- Validates wraparound-safe version comparison logic

## Adding New Tests

1. Create a new `*_test.cpp` file in this directory
2. Include the Unity framework: `#include <unity.h>`
3. Include the module under test: `#include <module_name.h>`
4. Write test functions using Unity assertions:
   - `TEST_ASSERT_TRUE(condition)`
   - `TEST_ASSERT_FALSE(condition)`
   - `TEST_ASSERT_EQUAL(expected, actual)`
   - See [Unity documentation](https://github.com/ThrowTheSwitch/Unity) for more
5. Register tests in `run_*_tests()` function
6. Update `platformio.ini` test configuration if needed

## Test Configuration (platformio.ini)

Add to your project's `platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -DUNIT_TEST
    -I../esp32common
lib_deps =
    throwtheswitch/Unity@^2.5.2

[env:esp32]
platform = espressif32
framework = arduino
board = esp32dev
test_framework = unity
```

## Continuous Integration

These tests can be integrated into CI/CD pipelines:

```yaml
# GitHub Actions example
- name: Run Unit Tests
  run: pio test -e native
```

## Test-Driven Development

When fixing bugs or adding features:

1. **Write a failing test** that reproduces the issue
2. **Fix the code** to make the test pass
3. **Verify** all existing tests still pass
4. **Document** the test case for future reference

## Phase 2.5 Testing Goals

- ✅ Version comparison wraparound handling
- ⏳ EspnowMessageRouter routing logic
- ⏳ EspnowSendUtils backoff timer behavior
- ⏳ EspnowPacketUtils checksum validation
- ⏳ VersionCompatibility range checking

## Notes

- Native tests run on your development machine (fast iteration)
- ESP32 tests require hardware but can test GPIO, WiFi, ESP-NOW, etc.
- Keep tests focused and independent (no shared state between tests)
- Use descriptive test names that explain what is being validated
