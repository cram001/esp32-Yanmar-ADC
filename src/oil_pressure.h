#pragma once

// ============================================================================
// OIL PRESSURE SENSOR — 5V ANALOG (0.5–4.5V)
// ============================================================================
//
/*
12V DC (boat)
 ├──► 5V Buck Converter
 │      ├──► +5V → Oil Pressure Sender V+
 │      └──► GND ─────────────────────────┐
 │                                         │
Oil Pressure Sender                        ESP32 FireBeetle
 ├── V+  ─────────────── +5V               ┌──────────────┐
 ├── GND ─────────────── GND ──────────────┤ GND          │
 └── SIGNAL ──┬── R1 = 10kΩ ──┐             │              │
               │              ├── ADC GPIO  │ ADC (GPIO36)│
               └── R2 = 10kΩ ─┘             │              │
                    │                       └──────────────┘
                   GND

*/

// Divider: 10k / 10k  → ADC sees 0.25–2.25 V
// Output: environment.engine.oilPressure (Pa)
// ============================================================================

#include <Arduino.h>

#include <sensesp/sensors/analog_input.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/signalk/signalk_output.h>

using namespace sensesp;

// -----------------------------------------------------------------------------
// SENSOR CONSTANTS
// -----------------------------------------------------------------------------
static constexpr float DIVIDER_RATIO  = 0.5f;

static constexpr float SENSOR_V_MIN   = 0.5f;
static constexpr float SENSOR_V_MAX   = 4.5f;
static constexpr float SENSOR_PSI_MAX = 100.0f;

// -----------------------------------------------------------------------------
// SETUP FUNCTION
// -----------------------------------------------------------------------------
inline void setup_oil_pressure_sensor(uint8_t adc_pin) {

  auto* oil_adc = new AnalogInput(
      adc_pin,
      200,
      "/Engine/OilPressure/ADC",
      1.0
  );

  // ADC volts → sensor volts
  auto* adc_to_sensor_v = new LambdaTransform<float, float>(
    [](float adc_v) {
      return adc_v / DIVIDER_RATIO;
    }
  );

  // Sensor volts → PSI
  auto* sensor_v_to_psi = new LambdaTransform<float, float>(
    [](float v) {
      v = constrain(v, SENSOR_V_MIN, SENSOR_V_MAX);
      return (v - SENSOR_V_MIN) *
             (SENSOR_PSI_MAX / (SENSOR_V_MAX - SENSOR_V_MIN));
    }
  );

  auto* psi_smooth = new MovingAverage(8);

  auto* psi_to_pa = new LambdaTransform<float, float>(
    [](float psi) {
      return psi * 6894.757f;
    }
  );

  oil_adc
    ->connect_to(adc_to_sensor_v)
    ->connect_to(sensor_v_to_psi)
    ->connect_to(psi_smooth)
    ->connect_to(psi_to_pa)
    ->connect_to(new SKOutputFloat(
        "environment.engine.oilPressure"
    ));
}
