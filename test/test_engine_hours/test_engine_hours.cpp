#include <unity.h>
#include <Arduino.h>

// Mock for testing engine hours logic
class MockEngineHours {
public:
    float hours = 0.0f;
    unsigned long last_update = 0;
    
    void update(float rpm, unsigned long current_millis) {
        if (last_update != 0 && rpm > 500.0f) {
            float dt_hours = (current_millis - last_update) / 3600000.0f;
            hours += dt_hours;
        }
        last_update = current_millis;
    }
    
    float get_hours() const {
        return float(int(hours * 10)) / 10.0f;  // Round to 0.1 hour
    }
};

MockEngineHours mock_hours;

void setUp(void) {
    mock_hours.hours = 0.0f;
    mock_hours.last_update = 0;
}

void tearDown(void) {
    // Cleanup
}

// ============================================================================
// TEST: Hour Accumulation Logic
// ============================================================================

void test_hours_accumulate_when_rpm_above_threshold(void) {
    mock_hours.update(1000.0f, 1000);      // Start at 1 second
    mock_hours.update(1000.0f, 3601000);   // 1 hour later
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, result);
}

void test_hours_do_not_accumulate_when_rpm_below_threshold(void) {
    mock_hours.update(400.0f, 1000);       // Below 500 RPM threshold
    mock_hours.update(400.0f, 3601000);    // 1 hour later
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_hours_do_not_accumulate_on_first_update(void) {
    mock_hours.update(1000.0f, 1000);      // First update
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_hours_accumulate_incrementally(void) {
    mock_hours.update(1000.0f, 0);
    mock_hours.update(1000.0f, 1800000);   // 0.5 hours
    
    float result1 = mock_hours.get_hours();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.5f, result1);
    
    mock_hours.update(1000.0f, 3600000);   // Another 0.5 hours
    
    float result2 = mock_hours.get_hours();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, result2);
}

// ============================================================================
// TEST: Time Conversion
// ============================================================================

void test_milliseconds_to_hours_conversion(void) {
    // 3600000 ms = 1 hour
    float hours = 3600000.0f / 3600000.0f;
    
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, hours);
}

void test_hours_to_seconds_conversion(void) {
    // 1 hour = 3600 seconds
    float seconds = 1.0f * 3600.0f;
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3600.0f, seconds);
}

void test_partial_hour_conversion(void) {
    // 1800000 ms = 0.5 hours = 1800 seconds
    float hours = 1800000.0f / 3600000.0f;
    float seconds = hours * 3600.0f;
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1800.0f, seconds);
}

// ============================================================================
// TEST: Rounding Logic
// ============================================================================

void test_hours_rounded_to_tenth(void) {
    mock_hours.hours = 1.234f;
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, result);
}

void test_hours_rounded_up(void) {
    mock_hours.hours = 1.267f;
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, result);  // Truncates, doesn't round up
}

// ============================================================================
// TEST: RPM Threshold
// ============================================================================

void test_rpm_threshold_exactly_500(void) {
    mock_hours.update(500.0f, 1000);
    mock_hours.update(500.0f, 3601000);
    
    float result = mock_hours.get_hours();
    
    // At exactly 500 RPM, should NOT accumulate (> 500 required)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_rpm_threshold_501(void) {
    mock_hours.update(501.0f, 1000);
    mock_hours.update(501.0f, 3601000);
    
    float result = mock_hours.get_hours();
    
    // At 501 RPM, should accumulate
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, result);
}

// ============================================================================
// TEST: Edge Cases
// ============================================================================

void test_zero_rpm(void) {
    mock_hours.update(0.0f, 1000);
    mock_hours.update(0.0f, 3601000);
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_negative_rpm(void) {
    mock_hours.update(-100.0f, 1000);
    mock_hours.update(-100.0f, 3601000);
    
    float result = mock_hours.get_hours();
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_very_high_rpm(void) {
    mock_hours.update(5000.0f, 1000);
    mock_hours.update(5000.0f, 3601000);
    
    float result = mock_hours.get_hours();
    
    // Should still accumulate normally
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, result);
}

// ============================================================================
// MAIN - Run all tests
// ============================================================================

void setup() {
    delay(2000);
    
    UNITY_BEGIN();
    
    // Hour accumulation tests
    RUN_TEST(test_hours_accumulate_when_rpm_above_threshold);
    RUN_TEST(test_hours_do_not_accumulate_when_rpm_below_threshold);
    RUN_TEST(test_hours_do_not_accumulate_on_first_update);
    RUN_TEST(test_hours_accumulate_incrementally);
    
    // Time conversion tests
    RUN_TEST(test_milliseconds_to_hours_conversion);
    RUN_TEST(test_hours_to_seconds_conversion);
    RUN_TEST(test_partial_hour_conversion);
    
    // Rounding tests
    RUN_TEST(test_hours_rounded_to_tenth);
    RUN_TEST(test_hours_rounded_up);
    
    // RPM threshold tests
    RUN_TEST(test_rpm_threshold_exactly_500);
    RUN_TEST(test_rpm_threshold_501);
    
    // Edge case tests
    RUN_TEST(test_zero_rpm);
    RUN_TEST(test_negative_rpm);
    RUN_TEST(test_very_high_rpm);
    
    UNITY_END();
}

void loop() {
    // Nothing
}
