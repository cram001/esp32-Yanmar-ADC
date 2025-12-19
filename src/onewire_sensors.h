#pragma once

// -----------------------------------------------------------------------------
// OneWire / DS18B20 temperature sensors
//
// This module is responsible for:
//   • Reading DS18B20 sensors via OneWire
//   • Applying an optional linear calibration
//   • Publishing temperatures to Signal K
//   • Updating global backing variables used by the Status page
//
// NOTE:
//   The Status page in SensESP v3 must be driven by ValueProducers.
//   We therefore update globals here, which are bridged to producers
//   in main.cpp (see periodic emit in setup()).
// -----------------------------------------------------------------------------

#include <sensesp_onewire/onewire_temperature.h>
#include <sensesp/transforms/linear.h>
#include <sensesp/transforms/lambda_transform.h>   // Used to "tap" values
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;
using namespace sensesp::onewire;

// -----------------------------------------------------------------------------
// Global backing variables (defined in main.cpp)
//
// These are NOT used directly by the Status page anymore.
// Instead, main.cpp periodically emits them into FloatProducers.
// -----------------------------------------------------------------------------
extern float temp_elbow_c;
extern float temp_compartment_c;
extern float temp_alternator_c;

// -----------------------------------------------------------------------------
// Hardware / timing configuration (defined in main.cpp)
// -----------------------------------------------------------------------------
extern const uint8_t  PIN_TEMP_COMPARTMENT;
extern const uint8_t  PIN_TEMP_EXHAUST;
extern const uint8_t  PIN_TEMP_ALT_12V;
extern const uint32_t ONEWIRE_READ_DELAY_MS;

// -----------------------------------------------------------------------------
// Setup function
// -----------------------------------------------------------------------------
inline void setup_temperature_sensors() {

  // Each DallasTemperatureSensors instance owns one OneWire bus.
  // Using separate buses avoids address management and simplifies wiring.
  auto* s1 = new DallasTemperatureSensors(PIN_TEMP_COMPARTMENT);
  auto* s2 = new DallasTemperatureSensors(PIN_TEMP_EXHAUST);
  auto* s3 = new DallasTemperatureSensors(PIN_TEMP_ALT_12V);

  // ===========================================================================
  // ENGINE ROOM TEMPERATURE
  // ===========================================================================

  // Raw DS18B20 temperature (°C)
  auto* t1 = new OneWireTemperature(
      s1,
      ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/engine"
  );

  // Linear calibration stage (gain + offset)
  auto* t1_linear = new Linear(
      1.0,
      0.0,
      "/config/sensors/temperature/engine/linear"
  );

  // Tap transform:
  //  • copies the calibrated value into a global
  //  • returns the value unchanged so the pipeline continues
  auto* tap_engine = new LambdaTransform<float, float>(
    [](float v) {
      temp_compartment_c = v;
      return v;
    }
  );

  // Signal K output
  auto* sk_engine = new SKOutputFloat(
      "environment.inside.engineRoom.temperature",
      "/config/outputs/sk/engine_temp"
  );

  // Data flow:
  //   DS18B20 → calibration → tap → Signal K
  t1->connect_to(t1_linear)
     ->connect_to(tap_engine)
     ->connect_to(sk_engine);

  // Configuration UI entries
  ConfigItem(t1)
      ->set_title("Engine Room DS18B20")
      ->set_description("Temperature of the engine compartment")
      ->set_sort_order(110);

  ConfigItem(t1_linear)
      ->set_title("Engine Room DS18B20 calibration")
      ->set_sort_order(120);

  ConfigItem(sk_engine)
      ->set_title("Engine Room SK Path")
      ->set_sort_order(130);

  // ===========================================================================
  // EXHAUST ELBOW TEMPERATURE
  // ===========================================================================

  auto* t2 = new OneWireTemperature(
      s2,
      ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/exhaust"
  );

  auto* t2_linear = new Linear(
      1.0,
      0.0,
      "/config/sensors/temperature/exhaust/linear"
  );

  auto* tap_exhaust = new LambdaTransform<float, float>(
    [](float v) {
      temp_elbow_c = v;
      return v;
    }
  );

  // Primary Signal K path
  auto* sk_exhaust = new SKOutputFloat(
      "propulsion.engine.exhaustTemperature",
      "/config/outputs/sk/exhaust_temp"
  );

  // Secondary path for legacy / i70-style consumers
  auto* sk_exhaust_i70 = new SKOutputFloat(
      "propulsion.engine.transmission.oilTemperature",
      "/config/outputs/sk/transmission_temp"
  );

  // Main pipeline
  t2->connect_to(t2_linear)
     ->connect_to(tap_exhaust)
     ->connect_to(sk_exhaust);

  // Fan-out AFTER the tap so both SK paths see identical values
  tap_exhaust->connect_to(sk_exhaust_i70);

  ConfigItem(t2)
      ->set_title("Exhaust elbow DS18B20")
      ->set_sort_order(200);

  ConfigItem(t2_linear)
      ->set_title("Exhaust elbow DS18B20 calibration")
      ->set_sort_order(201);

  ConfigItem(sk_exhaust)
      ->set_title("Exhaust elbow SK Path")
      ->set_sort_order(202);

  // ===========================================================================
  // ALTERNATOR TEMPERATURE
  // ===========================================================================

  auto* t3 = new OneWireTemperature(
      s3,
      ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/alternator"
  );

  auto* t3_linear = new Linear(
      1.0,
      0.0,
      "/config/sensors/temperature/alternator/linear"
  );

  auto* tap_alt = new LambdaTransform<float, float>(
    [](float v) {
      temp_alternator_c = v;
      return v;
    }
  );

  auto* sk_alt = new SKOutputFloat(
      "electrical.alternators.main.temperature",
      "/config/outputs/sk/alternator_temp"
  );

  t3->connect_to(t3_linear)
     ->connect_to(tap_alt)
     ->connect_to(sk_alt);

  ConfigItem(t3)
      ->set_title("Alternator DS18B20")
      ->set_sort_order(300);

  ConfigItem(t3_linear)
      ->set_title("Alternator DS18B20 calibration")
      ->set_sort_order(301);

  ConfigItem(sk_alt)
      ->set_title("Alternator SK Path")
      ->set_sort_order(302);
}
