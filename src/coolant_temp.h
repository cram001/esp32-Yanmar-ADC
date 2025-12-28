#pragma once

// ============================================================================
// COOLANT TEMPERATURE — ADC-BASED, PERIODIC, STABLE OUTPUT
// (SensESP v3.1.x compatible)
//
// Policy:
//   • ADC volts → temperature via empirical table
//   • Median filter + 5 s moving average
//   • Output emitted at 2 Hz regardless of value change
//   • Momentary ADC NaN / out-of-range is ignored
//   • Last valid temperature is frozen and continues to emit
//
// ============================================================================

#include <cmath>
#include <set>

#include <sensesp/transforms/linear.h>
#include <sensesp/transforms/median.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

#include "calibrated_analog_input.h"

using namespace sensesp;

// -----------------------------------------------------------------------------
// Externals from main.cpp
// -----------------------------------------------------------------------------
extern const uint8_t PIN_ADC_COOLANT;
extern const float   ADC_SAMPLE_RATE_HZ;

// -----------------------------------------------------------------------------
// ADC validity domain (used only to qualify updates)
// -----------------------------------------------------------------------------
constexpr float ADC_MIN_VALID_V = 0.257f;   // ≈121 °C
constexpr float ADC_MAX_VALID_V = 1.392f;   // ≈10 °C

// -----------------------------------------------------------------------------
// Engine coolant temperature
// -----------------------------------------------------------------------------
inline void setup_coolant_sender() {

  // ---------------------------------------------------------------------------
  // ADC tracking
  // ---------------------------------------------------------------------------
  static float last_adc_v = NAN;
  static float last_valid_temp_K = NAN;

  // ---------------------------------------------------------------------------
  // STEP 1 — Calibrated ADC input
  // ---------------------------------------------------------------------------
  auto* adc_raw = new CalibratedAnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/config/sensors/coolant/adc_raw"
  );
  adc_raw->enable();

  adc_raw->publish_calibration_mode(
      "debug.coolant.adc_calibration"
  );

  // Track raw ADC value (side-channel only)
  adc_raw->connect_to(
      new LambdaTransform<float, float>(
          [](float v) {
            last_adc_v = v;
            return v;
          }
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 2 — Median filter
  // ---------------------------------------------------------------------------
  auto* adc_v_smooth = adc_raw->connect_to(
      new Median(
          5,
          "/config/sensors/coolant/adc_v_smooth"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 3 — ADC volts → temperature (°C)
  // ---------------------------------------------------------------------------
  static std::set<CurveInterpolator::Sample> adc_to_temp = {

      { 0.257f, 121.0f },   // 250 °F, ~30 Ω

      { 0.762f,  80.6f },   // 177 °F
      { 0.766f,  80.0f },   // 176 °F
      { 0.786f,  79.4f },   // 175 °F
      { 0.814f,  78.3f },   // 173 °F
      { 0.843f,  76.7f },   // 170 °F
      { 0.898f,  71.1f },   // 160 °F
      { 0.925f,  67.8f },   // 154 °F
      { 0.941f,  66.7f },   // 152 °F
      { 0.943f,  66.1f },   // 151 °F
      { 1.040f,  60.0f },   // 140 °F
      { 1.136f,  52.2f },   // 126 °F
      { 1.212f,  48.9f },   // 120 °F
      { 1.309f,  40.6f },   // 105 °F
      { 1.392f,  10.0f }    // 50 °F
  };

  auto* temp_C = adc_v_smooth->connect_to(
      new CurveInterpolator(
          &adc_to_temp,
          "/config/sensors/coolant/temp_C"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 4 — Ignore NaN (do not poison pipeline)
  // ---------------------------------------------------------------------------
  auto* temp_C_safe = temp_C->connect_to(
      new LambdaTransform<float, float>(
          [](float c) {
            return std::isnan(c) ? 0.0f : c;
          },
          "/config/sensors/coolant/temp_C_safe"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 5 — Moving average (5 seconds)
  // ---------------------------------------------------------------------------
  const int MA_WINDOW =
      (ADC_SAMPLE_RATE_HZ * 5.0f < 1.0f)
          ? 1
          : static_cast<int>(ADC_SAMPLE_RATE_HZ * 5.0f);

  auto* temp_C_avg = temp_C_safe->connect_to(
      new MovingAverage(MA_WINDOW)
  );

  // ---------------------------------------------------------------------------
  // STEP 6 — °C → Kelvin
  // ---------------------------------------------------------------------------
  auto* temp_K = temp_C_avg->connect_to(
      new Linear(
          1.0f,
          273.15f,
          "/config/sensors/coolant/temp_K"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 7 — Signal K output (direct periodic emission)
  // ---------------------------------------------------------------------------
  auto* sk_coolant = new SKOutputFloat(
      "propulsion.engine.temperature",
      "/config/outputs/sk/coolant_temp"
  );

  ConfigItem(sk_coolant)
      ->set_title("Coolant Temperature (Engine)")
      ->set_sort_order(750);

  // Periodic emitter (2 Hz, freeze last valid)
  sensesp_app->get_event_loop()->onRepeat(
      500,
      [temp_K, sk_coolant]() {

        float v = temp_K->get();

        // Accept new value only if ADC is sane and value is valid
        if (!std::isnan(v) &&
            last_adc_v >= ADC_MIN_VALID_V &&
            last_adc_v <= ADC_MAX_VALID_V) {
          last_valid_temp_K = v;
        }

        // Emit last valid value if we have one
        if (!std::isnan(last_valid_temp_K)) {
          sk_coolant->emit(last_valid_temp_K);
        }
      }
  );

  // ---------------------------------------------------------------------------
  // STEP 8 — Debug outputs
  // ---------------------------------------------------------------------------
#if ENABLE_DEBUG_OUTPUTS
  adc_raw->connect_to(new SKOutputFloat("debug.coolant.adc_input_V"));
  adc_v_smooth->connect_to(new SKOutputFloat("debug.coolant.adc_filtered_V"));
  temp_C->connect_to(new SKOutputFloat("debug.coolant.temperature_C_raw"));
  temp_C_avg->connect_to(new SKOutputFloat("debug.coolant.temperature_C"));
  temp_K->connect_to(new SKOutputFloat("debug.coolant.temperature_K_raw"));
#endif
}
