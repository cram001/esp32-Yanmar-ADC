#pragma once

// ============================================================================
// COOLANT TEMPERATURE SENDER — YANMAR / AMERICAN TYPE D
//
// Electrical model (unchanged, proven):
//
//   13.5 V (gauge supply)
//      |
//   R_gauge  (≈1180–1212 Ω, inside gauge)
//      |
//      +----> Sender pin (THIS is what we measure)
//      |
//   R_sender (NTC to ground)
//      |
//   Engine block / GND
//
// Measurement:
//
//   • Sender pin voltage is tapped
//   • DFRobot 30k / 7.5k divider scales it by 1/5
//   • ESP32 ADC measures scaled voltage
//   • Software multiplies by 5 to recover sender-pin voltage
//
// Physics:
//
//   • Hot engine → low sender resistance → LOW voltage
//   • Cold engine → high sender resistance → HIGH voltage
//
// Output policy:
//
//   • Never emit NaN downstream
//   • If invalid / out of curve, clamp to 0 °C (273.15 K) sentinel
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
#include "sender_resistance.h"

using namespace sensesp;

// -----------------------------------------------------------------------------
// Externals from main.cpp
// -----------------------------------------------------------------------------
extern const uint8_t PIN_ADC_COOLANT;
extern const float   ADC_SAMPLE_RATE_HZ;

extern const float   COOLANT_DIVIDER_GAIN;     // = 5.0 (DFRobot)
extern const float   COOLANT_SUPPLY_VOLTAGE;   // = 13.5 V nominal
extern const float   COOLANT_GAUGE_RESISTOR;   // ≈1180–1212 Ω

// -----------------------------------------------------------------------------
// Engine coolant temperature sender
// -----------------------------------------------------------------------------
inline void setup_coolant_sender() {

  // ---------------------------------------------------------------------------
  // STEP 1 — Calibrated ADC input (returns volts at ESP32 ADC pin)
  // ESP32 internal ADC reference (~1.1 V), factory-calibrated if available
  // ---------------------------------------------------------------------------
  auto* adc_raw = new CalibratedAnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/config/sensors/coolant/adc_raw"
  );

  // IMPORTANT: custom Sensor<> subclasses are NOT auto-enabled by SensESP.
  adc_raw->enable();

  // Publish ADC calibration mode to Signal K (static debug metadata)
  adc_raw->publish_calibration_mode(
      "debug.coolant.adc_calibration"
  );

  // ---------------------------------------------------------------------------
  // STEP 2 — Raw ADC volts (DFRobot output voltage ≈ sender_pin / 5)
  // ---------------------------------------------------------------------------
  auto* volts_raw = adc_raw->connect_to(
      new Linear(
          1.0f,
          0.0f,
          "/config/sensors/coolant/volts_raw"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 3 — Recover sender-pin voltage (undo 5:1 divider)
  // ---------------------------------------------------------------------------
  auto* sender_voltage = volts_raw->connect_to(
      new Linear(
          COOLANT_DIVIDER_GAIN,   // ×5
          0.0f,
          "/config/sensors/coolant/sender_voltage"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 4 — Median filtering (noise rejection)
  // ---------------------------------------------------------------------------
  auto* sender_voltage_smooth = sender_voltage->connect_to(
      new Median(
          5,
          "/config/sensors/coolant/sender_voltage_smooth"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 5 — Sender voltage → sender resistance (Ω)
  //
  //   R_sender = R_gauge × V_sender / (V_supply − V_sender)
  //
  // SenderResistance may emit NaN on fault / out-of-range.
  // ---------------------------------------------------------------------------
  auto* sender_resistance = sender_voltage_smooth->connect_to(
      new SenderResistance(
          COOLANT_SUPPLY_VOLTAGE,
          COOLANT_GAUGE_RESISTOR,
          "/config/sensors/coolant/gauge_resistance"
      )
  );

  ConfigItem(sender_resistance)
      ->set_title("Coolant Gauge Coil (ohm)")
      ->set_description("Internal resistance of the helm coolant gauge; adjust to match your gauge so sender ohms are computed correctly")
      ->set_sort_order(325);

  // ---------------------------------------------------------------------------
  // STEP 6 – User-adjustable resistance trim (fine calibration)
  // ---------------------------------------------------------------------------
  auto* sender_res_scaled = sender_resistance->connect_to(
      new Linear(
          1.0f,
          0.0f,
          "/config/sensors/coolant/resistance_scale"
      )
  );

  ConfigItem(sender_res_scaled)
      ->set_title("Coolant Sender Resistance Scale")
      ->set_description("Fine trim applied to calculated sender resistance")
      ->set_sort_order(350);

  // ---------------------------------------------------------------------------
  // STEP 7 — Sender resistance → coolant temperature (°C)
  // American resistance Type-D curve
  // ---------------------------------------------------------------------------
  static std::set<CurveInterpolator::Sample> ohms_to_temp = {
      {  29.6f, 121.0f },
      {  45.0f, 100.0f },
      {  85.5f,  85.0f },
      {  90.9f,  82.5f },
      {  99.0f,  79.0f},    
      { 104.0f,  76.7f },
      { 112.0f,  72.0f },
      { 131.0f,  63.9f },
      { 207.0f,  56.0f },
      { 450.0f,  38.0f },
      {1352.0f,  12.7f }
  };

  auto* temp_C = sender_res_scaled->connect_to(
      new CurveInterpolator(
          &ohms_to_temp,
          "/config/sensors/coolant/temp_C"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 8 — Clamp invalid temperatures to 0 °C (never emit NaN)
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
  // STEP 9 — Moving average (5 seconds @ 10 Hz)
  // ---------------------------------------------------------------------------
  auto* temp_C_avg = temp_C_safe->connect_to(
      new MovingAverage(50)
  );

  // ---------------------------------------------------------------------------
  // STEP 10 — °C → K (Signal K requires Kelvin)
  // ---------------------------------------------------------------------------
  auto* temp_K = temp_C_avg->connect_to(
      new Linear(
          1.0f,
          273.15f,
          "/config/sensors/coolant/temp_K"
      )
  );

  // ---------------------------------------------------------------------------
  // STEP 11 — Signal K output
  // ---------------------------------------------------------------------------
  auto* sk_coolant = new SKOutputFloat(
      "propulsion.engine.temperature",
      "/config/outputs/sk/coolant_temp"
  );

  temp_K->connect_to(sk_coolant);

  ConfigItem(sk_coolant)
      ->set_title("Coolant Temperature (Engine)")
      ->set_sort_order(750);

  // ---------------------------------------------------------------------------
  // STEP 12 — Debug outputs (human readable)
  // ---------------------------------------------------------------------------
#if ENABLE_DEBUG_OUTPUTS
  adc_raw->connect_to(new SKOutputFloat("debug.coolant.adc_input_V"));
  volts_raw->connect_to(new SKOutputFloat("debug.coolant.adc_output_V"));
  sender_voltage->connect_to(new SKOutputFloat("debug.coolant.sender_pin_V"));
  sender_resistance->connect_to(new SKOutputFloat("debug.coolant.sender_resistance_ohm"));
  temp_C->connect_to(new SKOutputFloat("debug.coolant.temperature_C_raw"));
  temp_C_avg->connect_to(new SKOutputFloat("debug.coolant.temperature_C"));
  temp_K->connect_to(new SKOutputFloat("debug.coolant.temperature_K"));
#endif
}
