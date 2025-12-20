// FOR Yanmar COOLANT TEMP SENDER:
// DFROBOT Gravity Voltage Divider (SEN0003 / DFR0051)
// R1 = 30kΩ, R2 = 7.5kΩ → divider ratio = 1/5 → gain = 5.0
//
// ⚠️ VOLTAGE ANALYSIS (14.0V max system):
// - Normal operation (29-1352Ω sender): 0.30V - 1.49V ✅ SAFE
// - Open circuit fault (14.0V): 2.8V → EXCEEDS recommended 2.45V spec
// - ESP32 absolute max: 3.6V (won't damage, but out of spec)
// - RISK: Reduced accuracy/lifespan when >2.5V, vulnerable to voltage spikes
//
// - RECOMMENDED: Add 2.4V Zener diode for hardware protection (~$0.50)
// - Software protection: sender_resistance.h detects open circuit and emits NAN
//

#pragma once

#include <cmath>
#include <set>

#include <sensesp/sensors/analog_input.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/linear.h>
#include <sensesp/transforms/median.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>
#include <sensesp/ui/status_page_item.h>

#include "calibrated_analog_input.h"
#include "sender_resistance.h"

using namespace sensesp;

// -----------------------------------------------------------------------------
// Externals provided by main.cpp
// -----------------------------------------------------------------------------
extern const uint8_t PIN_ADC_COOLANT;
extern const float ADC_SAMPLE_RATE_HZ;
extern const float COOLANT_DIVIDER_GAIN;
extern const float COOLANT_SUPPLY_VOLTAGE;
extern const float COOLANT_GAUGE_RESISTOR;

// -----------------------------------------------------------------------------
// Status page items (defined in main.cpp)
// -----------------------------------------------------------------------------
extern StatusPageItem<float>* ui_coolant_adc_volts;
extern StatusPageItem<float>* ui_coolant_temp_c;

// -----------------------------------------------------------------------------
// Engine coolant temperature sender (RPM-independent)
// -----------------------------------------------------------------------------
inline void setup_coolant_sender() {

  // ---------------------------------------------------------------------------
  // STEP 1 — Calibrated ADC input (FireBeetle ESP32-E, 2.5 V reference)
  // ---------------------------------------------------------------------------
  auto* adc_raw = new CalibratedAnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/config/sensors/coolant/adc_raw");

  // ---------------------------------------------------------------------------
  // STEP 2 — Raw volts
  // ---------------------------------------------------------------------------
  auto* volts_raw = adc_raw->connect_to(
      new Linear(1.0f, 0.0f, "/config/sensors/coolant/volts_raw"));

  auto* volts_ui = volts_raw->connect_to(
      new LambdaTransform<float, float>([](float v) {
        if (ui_coolant_adc_volts) {
          ui_coolant_adc_volts->set(v);
        }
        return v;
      }));

  // ---------------------------------------------------------------------------
  // STEP 3 — Undo divider
  // ---------------------------------------------------------------------------
  auto* sender_voltage = volts_ui->connect_to(
      new Linear(COOLANT_DIVIDER_GAIN, 0.0f,
                 "/config/sensors/coolant/sender_voltage"));

  // ---------------------------------------------------------------------------
  // STEP 4 — Median filter
  // ---------------------------------------------------------------------------
  auto* sender_voltage_smooth = sender_voltage->connect_to(
      new Median(5, "/config/sensors/coolant/sender_voltage_smooth"));

  // ---------------------------------------------------------------------------
  // STEP 5 — Voltage → resistance
  // ---------------------------------------------------------------------------
  auto* sender_res_raw = sender_voltage_smooth->connect_to(
      new SenderResistance(
          COOLANT_SUPPLY_VOLTAGE,
          COOLANT_GAUGE_RESISTOR));

  // ---------------------------------------------------------------------------
  // STEP 6 — Resistance scale trim
  // ---------------------------------------------------------------------------
  auto* sender_res_scaled = sender_res_raw->connect_to(
      new Linear(1.0f, 0.0f,
                 "/config/sensors/coolant/resistance_scale"));

  ConfigItem(sender_res_scaled)
      ->set_title("Coolant Sender Resistance Scale")
      ->set_sort_order(350);

  // ---------------------------------------------------------------------------
  // STEP 7 — Resistance → °C
  // ---------------------------------------------------------------------------
  std::set<CurveInterpolator::Sample> ohms_to_temp = {
      {  29.6f, 121.0f },
      {  45.0f, 100.0f },
      {  85.5f,  85.0f },
      {  90.9f,  82.5f },
      {  97.0f,  80.0f },
      { 104.0f,  76.7f },
      { 112.0f,  72.0f },
      { 131.0f,  63.9f },
      { 207.0f,  56.0f },
      {1352.0f,  12.7f }
  };

  auto* temp_C = sender_res_scaled->connect_to(
      new CurveInterpolator(&ohms_to_temp,
                            "/config/sensors/coolant/temp_C"));

  // ---------------------------------------------------------------------------
  // STEP 8 — Moving average
  // ---------------------------------------------------------------------------
  auto* temp_C_avg = temp_C->connect_to(new MovingAverage(50));

  // UI branch — preserves NaN semantics
  auto* temp_C_ui = temp_C_avg->connect_to(
      new LambdaTransform<float, float>([](float c) {
        if (ui_coolant_temp_c) {
          ui_coolant_temp_c->set(c);
        }
        return c;
      }));
  (void)temp_C_ui;  // silence unused warning if any

  // ---------------------------------------------------------------------------
  // STEP 9 — °C → K (Signal K-safe)
  // ---------------------------------------------------------------------------
  // Signal K REQUIREMENT:
  //   - Path must see ONE finite value to exist
  //   - After that, NaN → null is correct and desired
  auto* temp_K_for_sk = temp_C_avg->connect_to(
      new LambdaTransform<float, float>(
          [](float c) {
            static bool sk_initialized = false;

            if (std::isnan(c)) {
              if (!sk_initialized) {
                sk_initialized = true;
                return 273.15f;  // 0 °C sentinel (one-time bootstrap)
              }
              return NAN;        // after bootstrap: NaN → null in Signal K
            }

            sk_initialized = true;
            return c + 273.15f;
          },
          "/config/sensors/coolant/temp_K_for_sk"));

  // ---------------------------------------------------------------------------
  // STEP 10 — Signal K output
  // ---------------------------------------------------------------------------
  auto* sk_coolant = new SKOutputFloat(
      "propulsion.engine.temperature",
      "/config/outputs/sk/coolant_temp");

  temp_K_for_sk->connect_to(sk_coolant);

  // ---------------------------------------------------------------------------
  // STEP 11 — Optional debug outputs
  // ---------------------------------------------------------------------------
#if ENABLE_DEBUG_OUTPUTS
  adc_raw->connect_to(new SKOutputFloat("debug.coolant.adc_volts"));
  volts_raw->connect_to(new SKOutputFloat("debug.coolant.volts_raw"));
  sender_voltage->connect_to(new SKOutputFloat("debug.coolant.senderVoltage"));
  sender_voltage_smooth->connect_to(
      new SKOutputFloat("debug.coolant.senderVoltage_smooth"));

  sender_res_raw
    ->connect_to(new LambdaTransform<float, float>(
        [](float r) { return std::isnan(r) ? -1.0f : r; }))
    ->connect_to(new SKOutputFloat("debug.coolant.senderResistance"));

  temp_C_avg
    ->connect_to(new LambdaTransform<float, float>(
        [](float c) { return std::isnan(c) ? -1000.0f : c; }))
    ->connect_to(new SKOutputFloat("debug.coolant.temperatureC_avg"));
#endif

  // ---------------------------------------------------------------------------
  // FINAL — Kick the pipeline ONCE after wiring exists
  // ---------------------------------------------------------------------------
  adc_raw->emit(NAN);
}
