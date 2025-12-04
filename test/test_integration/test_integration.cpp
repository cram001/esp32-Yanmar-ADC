#include <unity.h>
#include "../../src/sender_resistance.h"
#include <cmath>

// Integration tests - test multiple components working together

void setUp(void) {
    // Setup
}

void tearDown(void) {
    // Cleanup
}

// ============================================================================
// TEST: End-to-End Coolant Temperature Pipeline
// ============================================================================

void test_e2e_hot_engine_scenario(void) {
    // Scenario: Hot engine (121°C), 29.6Ω sender
    // Expected ADC voltage: ~0.066V
    
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(0.066f);
    float resistance = sender.get();
    
    // Should calculate ~29.6Ω
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 29.6f, resistance);
    
    // This resistance would map to 121°C in the curve interpolator
    // (tested separately in curve interpolator tests)
}

void test_e2e_cold_engine_scenario(void) {
    // Scenario: Cold engine (12.7°C), 1352Ω sender
    // Expected ADC voltage: ~1.442V
    
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(1.442f);
    float resistance = sender.get();
    
    // Should calculate ~1352Ω
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 1352.0f, resistance);
}

void test_e2e_operating_temperature_scenario(void) {
    // Scenario: Normal operating temp (~80°C), ~100Ω sender
    // Vtap = 13.5 * (100 / 1280) = 1.055V
    // ADC: 1.055V / 5 = 0.211V
    
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(0.211f);
    float resistance = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 100.0f, resistance);
}

// ============================================================================
// TEST: Voltage Variations Across Battery Range
// ============================================================================

void test_e2e_low_battery_voltage(void) {
    // 12.0V battery, 450Ω sender
    // Vtap = 12.0 * (450 / 1630) = 3.31V
    // ADC: 3.31V / 5 = 0.662V
    
    SenderResistance sender(12.0f, 1180.0f);
    sender.set(0.662f);
    float resistance = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 450.0f, resistance);
}

void test_e2e_high_battery_voltage(void) {
    // 14.0V battery, 450Ω sender
    // Vtap = 14.0 * (450 / 1630) = 3.86V
    // ADC: 3.86V / 5 = 0.772V
    
    SenderResistance sender(14.0f, 1180.0f);
    sender.set(0.772f);
    float resistance = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 450.0f, resistance);
}

void test_e2e_nominal_battery_voltage(void) {
    // 13.5V battery, 450Ω sender
    // Vtap = 13.5 * (450 / 1630) = 3.73V
    // ADC: 3.73V / 5 = 0.746V
    
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(0.746f);
    float resistance = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 450.0f, resistance);
}

// ============================================================================
// TEST: Fault Detection Integration
// ============================================================================

void test_e2e_open_circuit_detection(void) {
    // Open circuit: very high voltage
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(2.5f);
    float resistance = sender.get();
    
    // Should detect fault and return NaN
    TEST_ASSERT_TRUE(std::isnan(resistance));
}

void test_e2e_short_circuit_detection(void) {
    // Short circuit: very low voltage
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(0.015f);
    float resistance = sender.get();
    
    // Should detect fault and return NaN
    TEST_ASSERT_TRUE(std::isnan(resistance));
}

void test_e2e_disconnected_sensor_detection(void) {
    // Disconnected sensor: floating input
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(0.005f);  // Below threshold
    float resistance = sender.get();
    
    // Should detect fault and return NaN
    TEST_ASSERT_TRUE(std::isnan(resistance));
}

// ============================================================================
// TEST: Calibration Scenarios
// ============================================================================

void test_e2e_gauge_resistance_variation(void) {
    // Different gauge resistance (1000Ω instead of 1180Ω)
    // Same sender (450Ω), same supply (13.5V)
    // Vtap = 13.5 * (450 / 1450) = 4.19V
    // ADC: 4.19V / 5 = 0.838V
    
    SenderResistance sender(13.5f, 1000.0f);
    sender.set(0.838f);
    float resistance = sender.get();
    
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 450.0f, resistance);
}

void test_e2e_supply_voltage_calibration(void) {
    // Verify same sender resistance calculated at different voltages
    float sender_ohms = 450.0f;
    float gauge_ohms = 1180.0f;
    
    // 12V system
    float vtap_12v = 12.0f * (sender_ohms / (gauge_ohms + sender_ohms));
    float adc_12v = vtap_12v / 5.0f;
    
    SenderResistance sender_12v(12.0f, gauge_ohms);
    sender_12v.set(adc_12v);
    float result_12v = sender_12v.get();
    
    // 14V system
    float vtap_14v = 14.0f * (sender_ohms / (gauge_ohms + sender_ohms));
    float adc_14v = vtap_14v / 5.0f;
    
    SenderResistance sender_14v(14.0f, gauge_ohms);
    sender_14v.set(adc_14v);
    float result_14v = sender_14v.get();
    
    // Both should calculate same resistance
    TEST_ASSERT_FLOAT_WITHIN(10.0f, sender_ohms, result_12v);
    TEST_ASSERT_FLOAT_WITHIN(10.0f, sender_ohms, result_14v);
    TEST_ASSERT_FLOAT_WITHIN(10.0f, result_12v, result_14v);
}

// ============================================================================
// TEST: Real-World Scenarios
// ============================================================================

void test_e2e_engine_warmup_sequence(void) {
    // Simulate engine warming up from cold to operating temp
    SenderResistance sender(13.5f, 1180.0f);
    
    // Cold start: 1352Ω
    sender.set(1.442f);
    float r_cold = sender.get();
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 1352.0f, r_cold);
    
    // Warming: 450Ω
    sender.set(0.746f);
    float r_warm = sender.get();
    TEST_ASSERT_FLOAT_WITHIN(10.0f, 450.0f, r_warm);
    
    // Operating: 100Ω
    sender.set(0.211f);
    float r_hot = sender.get();
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 100.0f, r_hot);
    
    // Verify monotonic decrease
    TEST_ASSERT_GREATER_THAN(r_warm, r_cold);
    TEST_ASSERT_GREATER_THAN(r_hot, r_warm);
}

void test_e2e_voltage_spike_handling(void) {
    // Simulate voltage spike (alternator load dump)
    SenderResistance sender(13.5f, 1180.0f);
    
    // Normal reading
    sender.set(0.746f);
    float r_normal = sender.get();
    TEST_ASSERT_FALSE(std::isnan(r_normal));
    
    // Voltage spike (would be >14V at tap)
    sender.set(2.9f);
    float r_spike = sender.get();
    TEST_ASSERT_TRUE(std::isnan(r_spike));
    
    // Recovery to normal
    sender.set(0.746f);
    float r_recovery = sender.get();
    TEST_ASSERT_FALSE(std::isnan(r_recovery));
    TEST_ASSERT_FLOAT_WITHIN(10.0f, r_normal, r_recovery);
}

// ============================================================================
// TEST: System Boundaries
// ============================================================================

void test_e2e_minimum_valid_temperature(void) {
    // Coldest valid reading: 1352Ω (12.7°C)
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(1.442f);
    float resistance = sender.get();
    
    TEST_ASSERT_FALSE(std::isnan(resistance));
    TEST_ASSERT_GREATER_THAN(1300.0f, resistance);
}

void test_e2e_maximum_valid_temperature(void) {
    // Hottest valid reading: 29.6Ω (121°C)
    SenderResistance sender(13.5f, 1180.0f);
    sender.set(0.066f);
    float resistance = sender.get();
    
    TEST_ASSERT_FALSE(std::isnan(resistance));
    TEST_ASSERT_LESS_THAN(40.0f, resistance);
}

// ============================================================================
// MAIN - Run all tests
// ============================================================================

void setup() {
    delay(2000);
    
    UNITY_BEGIN();
    
    // End-to-end scenarios
    RUN_TEST(test_e2e_hot_engine_scenario);
    RUN_TEST(test_e2e_cold_engine_scenario);
    RUN_TEST(test_e2e_operating_temperature_scenario);
    
    // Battery voltage variations
    RUN_TEST(test_e2e_low_battery_voltage);
    RUN_TEST(test_e2e_high_battery_voltage);
    RUN_TEST(test_e2e_nominal_battery_voltage);
    
    // Fault detection
    RUN_TEST(test_e2e_open_circuit_detection);
    RUN_TEST(test_e2e_short_circuit_detection);
    RUN_TEST(test_e2e_disconnected_sensor_detection);
    
    // Calibration scenarios
    RUN_TEST(test_e2e_gauge_resistance_variation);
    RUN_TEST(test_e2e_supply_voltage_calibration);
    
    // Real-world scenarios
    RUN_TEST(test_e2e_engine_warmup_sequence);
    RUN_TEST(test_e2e_voltage_spike_handling);
    
    // System boundaries
    RUN_TEST(test_e2e_minimum_valid_temperature);
    RUN_TEST(test_e2e_maximum_valid_temperature);
    
    UNITY_END();
}

void loop() {
    // Nothing
}
