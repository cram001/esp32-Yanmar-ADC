#pragma once

// ============================================================================
// COOLANT TEMPERATURE — ADC-BASED, HEALTH-GATED, PERIODIC EMISSION
// (SensESP v3.1.x compatible)
// ============================================================================
//
// ADC volts → median → curve → moving average → Kelvin
//                       ↓
//               health / staleness gate
//                       ↓
//          periodic emit (≥ 1 Hz) → Signal K
//
// Validity rules:
//   • ADC update must be ≤ 2 seconds old
//   • ADC volts must be within calibrated domain
//
// Stale behavior:
//   • Emit NaN (Signal K null)
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
// ADC validity policy
// -----------------------------------------------------------------------------
constexpr float    ADC_MIN_VALID_V = 0.257f;   // ~121 °C
constexpr float    ADC_MAX_VALID_V = 1.392f;   // ~10 °C
constexpr uint32_t ADC_STALE_MS    = 2000;     // 2 seconds

// -----------------------------------------------------------------------------
// Engine coolant temperature
// -----------------------------------------------------------------------------
inline void setup_coolant_sender() {

  // ---------------------------------------------------------------------------
  // ADC health tracking
  // ---------------------------------------------------------------------------
  static float    last_adc_v  = NAN;
  static uint32_t last_adc_ms = 0;

  // ---------------------------------------------------------------------------
  // STEP 1 — Calibrated ADC input (volts at ESP32 pin)
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

  // Capture ADC freshness + value (side-channel, no math change)
  adc_raw->connect_to(
      new LambdaTransform<float, float>(
          [](float v) {
            last_adc_v  = v;
            last_adc_ms = millis();
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

      { 0.257f, 121.0f },   // 250 °F,  ~30 Ω

      { 0.762f,  80.6f },   // 177 °F,  ~105 Ω
      { 0.766f,  80.0f },   // 176 °F
      { 0.786f,  79.4f },   // 175 °F
      { 0.814f,  78.3f },   // 173 °F
      { 0.843f,  76.7f },   // 170 °F,  ~131 Ω
      { 0.898f,  71.1f },   // 160 °F,  ~158 Ω
      { 0.925f,  67.8f },   // 154 °F,  158.3 Ω
      { 0.941f,  66.7f },   // 152 °F
      { 0.943f,  66.1f },   // 151 °F
      { 1.040f,  60.0f },   // 140 °F
      { 1.136f,  52.2f },   // 126 °F
      { 1.212f,  48.9f },   // 120 °F
      { 1.309f,  40.6f },   // 105 °F
      { 1.392f,  10.0f }    // 50 °F,  1745 Ω
  };

  auto* temp_C = adc_v_smooth->connect_to(
      new CurveInterpolator(
          &adc_to_temp,
          "/config/sensors/coolant/temp_C"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 4 — Never propagate NaN
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
  // STEP 7 — Periodic health-gated emitter (≥ 1 Hz)
  // ---------------------------------------------------------------------------
  auto* temp_K_gate = new ObservableValue<float>(NAN);

  sensesp_app->get_event_loop()->onRepeat(
      500,   // 2 Hz → never slower than 1 Hz
      [temp_K, temp_K_gate]() {

        const uint32_t now = millis();

        // ADC stale
        if ((now - last_adc_ms) > ADC_STALE_MS) {
          temp_K_gate->emit(NAN);
          return;
        }

        // ADC out of calibrated domain
        if (last_adc_v < ADC_MIN_VALID_V ||
            last_adc_v > ADC_MAX_VALID_V) {
          temp_K_gate->emit(NAN);
          return;
        }

        // Valid → forward latest temperature
        float v = temp_K->get();
        if (!std::isnan(v)) {
          temp_K_gate->emit(v);
        }
      }
  );

  // ---------------------------------------------------------------------------
  // STEP 8 — Signal K output
  // ---------------------------------------------------------------------------
  auto* sk_coolant = new SKOutputFloat(
      "propulsion.engine.temperature",
      "/config/outputs/sk/coolant_temp"
  );

  temp_K_gate->connect_to(sk_coolant);

  ConfigItem(sk_coolant)
      ->set_title("Coolant Temperature (Engine)")
      ->set_sort_order(750);

  // ---------------------------------------------------------------------------
  // STEP 9 — Debug outputs
  // ---------------------------------------------------------------------------
#if ENABLE_DEBUG_OUTPUTS
  adc_raw->connect_to(new SKOutputFloat("debug.coolant.adc_input_V"));
  adc_v_smooth->connect_to(new SKOutputFloat("debug.coolant.adc_filtered_V"));
  temp_C->connect_to(new SKOutputFloat("debug.coolant.temperature_C_raw"));
  temp_C_avg->connect_to(new SKOutputFloat("debug.coolant.temperature_C"));
  temp_K_gate->connect_to(new SKOutputFloat("debug.coolant.temperature_K"));
#endif
}
