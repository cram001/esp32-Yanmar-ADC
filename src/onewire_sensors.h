#pragma once

#include <sensesp_onewire/onewire_temperature.h>
#include <sensesp/transforms/linear.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;
using namespace sensesp::onewire;

// These are provided by main.cpp
extern const uint8_t PIN_TEMP_COMPARTMENT;
extern const uint8_t PIN_TEMP_EXHAUST;
extern const uint8_t PIN_TEMP_ALT_12V;
extern const uint32_t ONEWIRE_READ_DELAY_MS;

// -----------------------------------------------------------------------------
// OneWire / DS18B20 temperature sensors
// -----------------------------------------------------------------------------
inline void setup_temperature_sensors() {

  auto* s1 = new DallasTemperatureSensors(PIN_TEMP_COMPARTMENT);
  auto* s2 = new DallasTemperatureSensors(PIN_TEMP_EXHAUST);
  auto* s3 = new DallasTemperatureSensors(PIN_TEMP_ALT_12V);

  // ========================= ENGINE ROOM ==============================

  auto* t1 = new OneWireTemperature(
      s1, ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/engine"
  );

  auto* t1_linear = new Linear(
      1.0, 0.0,
      "/config/sensors/temperature/engine/linear"
  );

  auto* sk_engine = new SKOutputFloat(
      "environment.inside.engineRoom.temperature",
      "/config/outputs/sk/engine_temp"
  );

  t1->connect_to(t1_linear)->connect_to(sk_engine);

  ConfigItem(t1)
      ->set_title("Engine Room DS18B20")
      ->set_description("Temperature of the engine compartment");

  ConfigItem(t1_linear)
      ->set_title("Engine Room DS18B20 calibration");

  ConfigItem(sk_engine)
      ->set_title("Engine Room SK Path");

  // ========================= EXHAUST ==============================

  auto* t2 = new OneWireTemperature(
      s2, ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/exhaust"
  );

  auto* t2_linear = new Linear(
      1.0, 0.0,
      "/config/sensors/temperature/exhaust/linear"
  );

  auto* sk_exhaust_i70 = new SKOutputFloat(
      "propulsion.engine.transmision.oilTemperature",
      "/config/outputs/sk/transmission_temp"
  );

  auto* sk_exhaust = new SKOutputFloat(
      "propulsion.engine.exhaustTemperature",
      "/config/outputs/sk/exhaust_temp"
  );

  t2->connect_to(t2_linear)->connect_to(sk_exhaust_i70);
  t2->connect_to(t2_linear)->connect_to(sk_exhaust);

  ConfigItem(t2)
      ->set_title("Exhaust DS18B20");

  // ========================= ALTERNATOR ==============================

  auto* t3 = new OneWireTemperature(
      s3, ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/alternator"
  );

  auto* t3_linear = new Linear(
      1.0, 0.0,
      "/config/sensors/temperature/alternator/linear"
  );

  auto* sk_alt = new SKOutputFloat(
      "electrical.alternators.main.temperature",
      "/config/outputs/sk/alternator_temp"
  );

  t3->connect_to(t3_linear)->connect_to(sk_alt);

  ConfigItem(t3)
      ->set_title("Alternator DS18B20");
}
