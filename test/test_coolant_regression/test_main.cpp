#include <Arduino.h>
#include <unity.h>

// We want access to the coolant simulator math helpers.
// Ensure the header content is enabled during unit tests.
#ifndef COOLANT_SIMULATOR
#define COOLANT_SIMULATOR 1
#endif

#include "coolant_simulator.h"

// If you later wrapped your simulator in a namespace, change calls like:
//   ohms_for_temp(...) -> coolant_sim::ohms_for_temp(...)

static void test_ohms_endpoints() {
  // Endpoints from your curve table (should match closely)
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1352.0f, ohms_for_temp(12.7f));
  TEST_ASSERT_FLOAT_WITHIN(0.5f,  207.0f, ohms_for_temp(56.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.5f,   97.0f, ohms_for_temp(80.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.5f,   85.5f, ohms_for_temp(85.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.5f,   45.0f, ohms_for_temp(100.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.5f,   29.6f, ohms_for_temp(121.0f));
}

static void test_ohms_monotonic_decreasing() {
  // Hotter -> lower ohms (non-increasing)
  float prev = ohms_for_temp(12.7f);
  for (float t = 15.0f; t <= 95.0f; t += 5.0f) {
    float now = ohms_for_temp(t);
    TEST_ASSERT_TRUE_MESSAGE(now <= (prev + 1e-3f), "ohms should not increase as temp rises");
    prev = now;
  }
}

static void test_adc_volts_sane_range() {
  for (float t = 12.0f; t <= 95.0f; t += 5.0f) {
    float v = adc_volts_for_temp(t);
    TEST_ASSERT_TRUE_MESSAGE(v >= 0.0f, "ADC volts must be >= 0");
    TEST_ASSERT_TRUE_MESSAGE(v <= 3.3f, "ADC volts must be <= 3.3");
  }
}

static void test_adc_volts_monotonic_decreasing() {
  // Hotter -> lower sender resistance -> lower sender node voltage -> lower ADC volts
  float prev = adc_volts_for_temp(12.7f);
  for (float t = 15.0f; t <= 95.0f; t += 5.0f) {
    float now = adc_volts_for_temp(t);
    TEST_ASSERT_TRUE_MESSAGE(now <= (prev + 1e-4f), "ADC volts should not increase as temp rises");
    prev = now;
  }
}

static void test_adc_known_points() {
  // These anchors validate the full math:
  // Vsupply=13.5V, Rgauge=1180Ω, divider gain=5.0
  // Expected ADC-pin volts (approx):
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 1.44f, adc_volts_for_temp(12.7f));
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.403f, adc_volts_for_temp(56.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.205f, adc_volts_for_temp(80.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.099f, adc_volts_for_temp(100.0f));
}

void setup() {
  delay(1500); // give Serial time to come up on some boards

  UNITY_BEGIN();
  RUN_TEST(test_ohms_endpoints);
  RUN_TEST(test_ohms_monotonic_decreasing);
  RUN_TEST(test_adc_volts_sane_range);
  RUN_TEST(test_adc_volts_monotonic_decreasing);
  RUN_TEST(test_adc_known_points);
  UNITY_END();
}

void loop() {
  // nothing
}
