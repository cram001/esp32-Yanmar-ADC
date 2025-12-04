#include <unity.h>
#include <cmath>

// Mock engine load calculations
namespace EngineLoad {
    const float BSFC_G_PER_KWH = 240.0f;
    const float FUEL_DENSITY_KG_PER_L = 0.84f;
    
    float calculate_power_from_fuel(float lph) {
        if (std::isnan(lph) || lph <= 0.0f) {
            return NAN;
        }
        float fuel_kg_per_h = lph * FUEL_DENSITY_KG_PER_L;
        return (fuel_kg_per_h * 1000.0f) / BSFC_G_PER_KWH;
    }
    
    float calculate_load(float actual_kW, float max_kW) {
        if (std::isnan(actual_kW) || std::isnan(max_kW) || max_kW <= 0.0f) {
            return NAN;
        }
        return actual_kW / max_kW;
    }
}

void setUp(void) {
    // Setup
}

void tearDown(void) {
    // Cleanup
}

// ============================================================================
// TEST: Power Calculation from Fuel Rate
// ============================================================================

void test_power_calculation_typical_fuel_rate(void) {
    // 2.5 L/h at 0.84 kg/L = 2.1 kg/h
    // Power = (2.1 kg/h * 1000 g/kg) / 240 g/kWh = 8.75 kW
    
    float power = EngineLoad::calculate_power_from_fuel(2.5f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 8.75f, power);
}

void test_power_calculation_low_fuel_rate(void) {
    // 0.7 L/h (idle)
    // Power = (0.7 * 0.84 * 1000) / 240 = 2.45 kW
    
    float power = EngineLoad::calculate_power_from_fuel(0.7f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.45f, power);
}

void test_power_calculation_high_fuel_rate(void) {
    // 6.5 L/h (full throttle)
    // Power = (6.5 * 0.84 * 1000) / 240 = 22.75 kW
    
    float power = EngineLoad::calculate_power_from_fuel(6.5f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 22.75f, power);
}

void test_power_calculation_zero_fuel(void) {
    float power = EngineLoad::calculate_power_from_fuel(0.0f);
    
    TEST_ASSERT_TRUE(std::isnan(power));
}

void test_power_calculation_negative_fuel(void) {
    float power = EngineLoad::calculate_power_from_fuel(-1.0f);
    
    TEST_ASSERT_TRUE(std::isnan(power));
}

void test_power_calculation_nan_fuel(void) {
    float power = EngineLoad::calculate_power_from_fuel(NAN);
    
    TEST_ASSERT_TRUE(std::isnan(power));
}

// ============================================================================
// TEST: Load Fraction Calculation
// ============================================================================

void test_load_calculation_50_percent(void) {
    float load = EngineLoad::calculate_load(15.0f, 30.0f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, load);
}

void test_load_calculation_25_percent(void) {
    float load = EngineLoad::calculate_load(7.5f, 30.0f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, load);
}

void test_load_calculation_100_percent(void) {
    float load = EngineLoad::calculate_load(30.0f, 30.0f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, load);
}

void test_load_calculation_over_100_percent(void) {
    // Shouldn't happen in practice, but test the math
    float load = EngineLoad::calculate_load(35.0f, 30.0f);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.167f, load);
}

void test_load_calculation_zero_max_power(void) {
    float load = EngineLoad::calculate_load(10.0f, 0.0f);
    
    TEST_ASSERT_TRUE(std::isnan(load));
}

void test_load_calculation_nan_actual_power(void) {
    float load = EngineLoad::calculate_load(NAN, 30.0f);
    
    TEST_ASSERT_TRUE(std::isnan(load));
}

void test_load_calculation_nan_max_power(void) {
    float load = EngineLoad::calculate_load(15.0f, NAN);
    
    TEST_ASSERT_TRUE(std::isnan(load));
}

// ============================================================================
// TEST: BSFC Constants
// ============================================================================

void test_bsfc_constant_value(void) {
    // Verify BSFC is 240 g/kWh (typical for diesel)
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 240.0f, EngineLoad::BSFC_G_PER_KWH);
}

void test_fuel_density_constant_value(void) {
    // Verify diesel density is 0.84 kg/L
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.84f, EngineLoad::FUEL_DENSITY_KG_PER_L);
}

// ============================================================================
// TEST: Unit Conversions
// ============================================================================

void test_liters_to_kilograms_conversion(void) {
    // 1 L diesel = 0.84 kg
    float kg = 1.0f * EngineLoad::FUEL_DENSITY_KG_PER_L;
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.84f, kg);
}

void test_kilograms_to_grams_conversion(void) {
    // 1 kg = 1000 g
    float grams = 1.0f * 1000.0f;
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f, grams);
}

// ============================================================================
// TEST: Yanmar 3JH3E Specific Values
// ============================================================================

void test_max_power_at_3800_rpm(void) {
    // Yanmar 3JH3E: 40 hp @ 3800 RPM = 29.83 kW
    float max_power_kW = 29.83f;
    
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 29.83f, max_power_kW);
}

void test_typical_cruise_load(void) {
    // Typical cruise: 2.5 L/h fuel, ~20 kW max at cruise RPM
    float actual_power = EngineLoad::calculate_power_from_fuel(2.5f);
    float load = EngineLoad::calculate_load(actual_power, 20.0f);
    
    // Should be around 40-50% load
    TEST_ASSERT_GREATER_THAN(0.3f, load);
    TEST_ASSERT_LESS_THAN(0.6f, load);
}

void test_idle_load(void) {
    // Idle: 0.7 L/h fuel, ~18 kW max at idle RPM
    float actual_power = EngineLoad::calculate_power_from_fuel(0.7f);
    float load = EngineLoad::calculate_load(actual_power, 18.0f);
    
    // Should be very low load
    TEST_ASSERT_LESS_THAN(0.2f, load);
}

// ============================================================================
// MAIN - Run all tests
// ============================================================================

void setup() {
    delay(2000);
    
    UNITY_BEGIN();
    
    // Power calculation tests
    RUN_TEST(test_power_calculation_typical_fuel_rate);
    RUN_TEST(test_power_calculation_low_fuel_rate);
    RUN_TEST(test_power_calculation_high_fuel_rate);
    RUN_TEST(test_power_calculation_zero_fuel);
    RUN_TEST(test_power_calculation_negative_fuel);
    RUN_TEST(test_power_calculation_nan_fuel);
    
    // Load calculation tests
    RUN_TEST(test_load_calculation_50_percent);
    RUN_TEST(test_load_calculation_25_percent);
    RUN_TEST(test_load_calculation_100_percent);
    RUN_TEST(test_load_calculation_over_100_percent);
    RUN_TEST(test_load_calculation_zero_max_power);
    RUN_TEST(test_load_calculation_nan_actual_power);
    RUN_TEST(test_load_calculation_nan_max_power);
    
    // Constants tests
    RUN_TEST(test_bsfc_constant_value);
    RUN_TEST(test_fuel_density_constant_value);
    
    // Unit conversion tests
    RUN_TEST(test_liters_to_kilograms_conversion);
    RUN_TEST(test_kilograms_to_grams_conversion);
    
    // Yanmar-specific tests
    RUN_TEST(test_max_power_at_3800_rpm);
    RUN_TEST(test_typical_cruise_load);
    RUN_TEST(test_idle_load);
    
    UNITY_END();
}

void loop() {
    // Nothing
}
