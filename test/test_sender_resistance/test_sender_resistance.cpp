#include <unity.h>
#include "../../src/sender_resistance.h"
#include <cmath>

// Test fixture - runs before each test
void setUp(void) {
    // Set up code here
}

// Test fixture - runs after each test
void tearDown(void) {
    // Clean up code here
}

// ============================================================================
// TEST: Input Validation
// ============================================================================

void test_sender_resistance_rejects_nan_input(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    sender.set(NAN);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

void test_sender_resistance_rejects_infinite_input(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    sender.set(INFINITY);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

void test_sender_resistance_rejects_low_voltage(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    sender.set(0.005f);  // Below 0.01V threshold
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

void test_sender_resistance_rejects_impossible_voltage(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // After 5x divider, this would be 13.5V (equal to supply)
    sender.set(2.7f);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

// ============================================================================
// TEST: Voltage Divider Compensation
// ============================================================================

void test_voltage_divider_compensation_correctness(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Known good case: 0.5V ADC input
    // After 5x divider: 2.5V at tap
    // With 13.5V supply and 1180Ω gauge:
    // Rsender = 1180 * (2.5 / (13.5 - 2.5))
    // Rsender = 1180 * (2.5 / 11.0)
    // Rsender = 268.18Ω
    
    sender.set(0.5f);
    float result = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 268.18f, result);
}

// ============================================================================
// TEST: Series Circuit Calculation
// ============================================================================

void test_series_circuit_hot_engine(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Hot engine: 29.6Ω sender
    // Vtap = 13.5 * (29.6 / (1180 + 29.6)) = 0.33V
    // ADC sees: 0.33V / 5 = 0.066V
    
    sender.set(0.066f);
    float result = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 29.6f, result);
}

void test_series_circuit_cold_engine(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Cold engine: 1352Ω sender
    // Vtap = 13.5 * (1352 / (1180 + 1352)) = 7.21V
    // ADC sees: 7.21V / 5 = 1.442V
    
    sender.set(1.442f);
    float result = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 1352.0f, result);
}

void test_series_circuit_mid_range(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Mid-range: 450Ω sender (typical operating temp)
    // Vtap = 13.5 * (450 / (1180 + 450)) = 3.73V
    // ADC sees: 3.73V / 5 = 0.746V
    
    sender.set(0.746f);
    float result = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 450.0f, result);
}

// ============================================================================
// TEST: Resistance Range Validation
// ============================================================================

void test_resistance_rejects_short_circuit(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Very low voltage that would calculate to < 20Ω
    sender.set(0.015f);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

void test_resistance_rejects_open_circuit(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Very high voltage that would calculate to > 20kΩ
    sender.set(2.5f);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

void test_resistance_accepts_valid_range(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    // Valid mid-range value
    sender.set(0.5f);
    float result = sender.get();
    
    TEST_ASSERT_FALSE(std::isnan(result));
    TEST_ASSERT_GREATER_THAN(20.0f, result);
    TEST_ASSERT_LESS_THAN(20000.0f, result);
}

// ============================================================================
// TEST: Supply Voltage Independence
// ============================================================================

void test_supply_voltage_independence_12v(void) {
    SenderResistance sender_12v(12.0f, 1180.0f);
    
    // 450Ω sender at 12V
    // Vtap = 12.0 * (450 / 1630) = 3.31V
    // ADC: 3.31V / 5 = 0.662V
    
    sender_12v.set(0.662f);
    float result_12v = sender_12v.get();
    
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 450.0f, result_12v);
}

void test_supply_voltage_independence_14v(void) {
    SenderResistance sender_14v(14.0f, 1180.0f);
    
    // 450Ω sender at 14V
    // Vtap = 14.0 * (450 / 1630) = 3.86V
    // ADC: 3.86V / 5 = 0.772V
    
    sender_14v.set(0.772f);
    float result_14v = sender_14v.get();
    
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 450.0f, result_14v);
}

// ============================================================================
// TEST: Edge Cases
// ============================================================================

void test_zero_voltage_input(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    sender.set(0.0f);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

void test_negative_voltage_input(void) {
    SenderResistance sender(13.5f, 1180.0f);
    
    sender.set(-0.5f);
    float result = sender.get();
    
    TEST_ASSERT_TRUE(std::isnan(result));
}

// ============================================================================
// MAIN - Run all tests
// ============================================================================

void setup() {
    delay(2000);  // Wait for serial monitor
    
    UNITY_BEGIN();
    
    // Input validation tests
    RUN_TEST(test_sender_resistance_rejects_nan_input);
    RUN_TEST(test_sender_resistance_rejects_infinite_input);
    RUN_TEST(test_sender_resistance_rejects_low_voltage);
    RUN_TEST(test_sender_resistance_rejects_impossible_voltage);
    
    // Voltage divider tests
    RUN_TEST(test_voltage_divider_compensation_correctness);
    
    // Series circuit tests
    RUN_TEST(test_series_circuit_hot_engine);
    RUN_TEST(test_series_circuit_cold_engine);
    RUN_TEST(test_series_circuit_mid_range);
    
    // Range validation tests
    RUN_TEST(test_resistance_rejects_short_circuit);
    RUN_TEST(test_resistance_rejects_open_circuit);
    RUN_TEST(test_resistance_accepts_valid_range);
    
    // Supply voltage independence tests
    RUN_TEST(test_supply_voltage_independence_12v);
    RUN_TEST(test_supply_voltage_independence_14v);
    
    // Edge case tests
    RUN_TEST(test_zero_voltage_input);
    RUN_TEST(test_negative_voltage_input);
    
    UNITY_END();
}

void loop() {
    // Nothing here
}
