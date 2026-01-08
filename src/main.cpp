// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.1
// ============================================================================
// Features:
//  - 3x OneWire temperature sensors
//  - RPM via magnetic pickup (works in conjunction with gauge)
// -  Engine load (%) estimation based on RPM, speed (STW/SOG), wind (requires calibration for your boat)
//  - Engine coolant temp (via boat's coolant temp sender, requires gauge to be fitted  and working, for american sender range)
//  - Fuel flow (LPH) estimation based on RPM and engine load (requires calibration for your engine/boat)
//  - Oil pressure, for american sender resistance (via boat's oil pressure sender, requires gauge to be fitted and working)
//  - Engine hours accumulator, resetable via UI
//  - SK debug output
//  - OTA update
//  - Full UI configuration for SK paths and calibration, setting wifi and SK server address
// values sent to SignalK IAW https://signalk.org/specification/1.5.0/doc/vesselsBranch.html (note: minor errrors
//  in spec (Appendix A) must be corrected for compatibility with canboat.js )

#ifndef UNIT_TEST

#include <memory>

// SensESP core
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/signalk/signalk_value_listener.h"

// Config UI elements
#include "sensesp/ui/config_item.h"

// Sensors
#include "sensesp/sensors/analog_input.h"
#include "driver/adc.h"

// Transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/median.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/transforms/moving_average.h"
#include "sensesp/transforms/lambda_transform.h"

// Custom function
//#include "sender_resistance.h"  // not in use
#include "engine_hours.h"
#include "calibrated_analog_input.h"
#include "engine_fuel.h"
#include "onewire_sensors.h"
#include "coolant_temp.h"
#include "rpm_sensor.h"
#include "oil_pressure.h"

// Simulator/test options
#ifdef RPM_SIMULATOR
#include "rpm_simulator.h"
#endif

#if COOLANT_SIMULATOR
#include "coolant_simulator.h"
#endif

#if (RPM_SIMULATOR && COOLANT_SIMULATOR)
#error "Enable only one simulator at a time (both want GPIO26)."
#endif

using namespace sensesp;
using namespace sensesp::onewire;

// -----------------------------------------------
// FORWARD DECLARATIONS
// -----------------------------------------------
void setup_engine_hours();

// -----------------------------------------------
// PIN DEFINITIONS — FIREBEETLE ESP32-E   EDIT THESE IF REQUIRE FOR YOUR BOARD
// -----------------------------------------------
const uint8_t PIN_TEMP_COMPARTMENT = 4;   // one wire for engine compartment /digital  12
const uint8_t PIN_TEMP_EXHAUST     = 14;  // one wire,strapped to exhaust elbow digital 6
const uint8_t PIN_TEMP_ALT_12V     = 17;  // extra sensor... could be aft cabin?? / digital 10  - NOT USED

const uint8_t PIN_ADC_COOLANT      = 39;  // engine's coolant temperature sender
const uint8_t PIN_RPM              = 25;  // magnetic pickup for RPM (digital input) digital 2

const uint8_t PIN_ADC_OIL_PRESSURE = 36;  // choose free ADC pin

// OneWire sensor read delay (ms)
const uint32_t ONEWIRE_READ_DELAY_MS = 500;

// RPM sensor configuration (flywheel tooth count)
const float RPM_TEETH = 116.0f;

// RPM signal globals (defined here, used in rpm_sensor.h)
Frequency* g_frequency = nullptr;
ValueProducer<float>* g_engine_rev_s_smooth = nullptr;
ValueProducer<float>* g_engine_rad_s = nullptr;

// ---------------------------------------------------------------------------
// NOTE:
// The following oil-pressure constants are from the *resistive sender* design.
// They are retained for documentation only and are NOT used by the new
// 0.5–4.5V pressure transducer implementation.
// ---------------------------------------------------------------------------
const float OIL_ADC_REF_VOLTAGE = 2.5f;     // ref voltage of the Firebeetle esp32-e
const float OIL_PULLUP_RESISTOR = 220.0f;   // Resistance (ohm) in mid-range of oil px sender values

// -----------------------------------------------
// CONSTANTS
// -----------------------------------------------
const float ADC_SAMPLE_RATE_HZ = 1.0f;   // sample rate for coolant temp and oil pressure ADC pins (Hz)

// ============================================================================
// SETUP
// ============================================================================
void setup() {

#ifdef RPM_SIMULATOR
  Serial.println("RPM SIMULATOR ACTIVE");
  setupRPMSimulator();
#endif

#if COOLANT_SIMULATOR
  Serial.println("COOLANT SIMULATOR ACTIVE");
  setupCoolantSimulator();
#endif

  SetupLogging();

  SensESPAppBuilder builder;

  builder.set_hostname("esp32-yanmar")
      ->set_wifi_client("wifi_ssid", "wifi_passphrase")
      ->enable_ota("esp32!")
      ->set_sk_server("192.168.88.99", 3000);

  sensesp_app = builder.get_app();

  // setup engine performance inputs (from NMEA2000--> signalK --> sensesp)
  auto* stw = new SKValueListener<float>(
      "navigation.speedThroughWater",
      1000,
      "/config/inputs/stw"
  );

  auto* sog = new SKValueListener<float>(
      "navigation.speedOverGround",
      1000,
      "/config/inputs/sog"
  );

  auto* aws = new SKValueListener<float>(
      "environment.wind.speedApparent",
      1000,
      "/config/inputs/aws"
  );

  auto* awa = new SKValueListener<float>(
      "environment.wind.angleApparent",
      1000,
      "/config/inputs/awa"
  );

  // Call setup functions for sensors
  setup_temperature_sensors();
  setup_coolant_sender();
  setup_rpm_sensor();

  // -------------------------------------------------------------------------
  // Oil pressure sender (0.5–4.5V transducer → ADC → Signal K)
  // -------------------------------------------------------------------------
  setup_oil_pressure_sensor(PIN_ADC_OIL_PRESSURE);

  setup_engine_hours();

  setup_engine_fuel(
      g_engine_rev_s_smooth,  // stable revs
      stw,
      sog,
      aws,
      awa
  );

  sensesp_app->start();
}

// ============================================================================
// ENGINE HOURS — UI + EDITABLE SK PATH
// ============================================================================
#include "sensesp/transforms/linear.h"

void setup_engine_hours() {

  auto* hours = new EngineHours("/config/sensors/engine_hours");

  auto* hours_to_seconds = hours->connect_to(
      new Linear(3600.0f, 0.0f, "/config/sensors/engine_hours_to_seconds")
  );

  auto* sk_hours = new SKOutputFloat(
      "propulsion.engine.runTime",
      "/config/outputs/sk/engine_hours"
  );

  if (g_engine_rev_s_smooth != nullptr) {

    auto* revs_to_rpm = g_engine_rev_s_smooth->connect_to(
        new LambdaTransform<float,float>([](float rps) {
          return std::isnan(rps) ? NAN : (rps * 60.0f);
        })
    );

    revs_to_rpm->connect_to(hours);

  } else {
    Serial.println("Engine hours skipped: RPM signal unavailable");
  }

  // hours_to_seconds->connect_to(sk_hours);

  ConfigItem(hours)
      ->set_title("Engine Hours Accumulator")
      ->set_description("Tracks total engine run time in hours");

  ConfigItem(sk_hours)
      ->set_title("Engine Run Time SK Path")
      ->set_description("Signal K path for engine runtime (seconds)");
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {

#ifdef RPM_SIMULATOR
  static uint32_t last_debug = 0;
  if (millis() - last_debug > 200) {
    Serial.printf("Pulses: %u\n", pulse_count);
    last_debug = millis();
  }
#endif

  sensesp_app->get_event_loop()->tick();

#ifdef RPM_SIMULATOR
  loopRPMSimulator();
#endif

#if COOLANT_SIMULATOR
  loopCoolantSimulator();
#endif
}

#endif  // UNIT_TEST
