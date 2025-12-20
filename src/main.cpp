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

//  Note: you need a signalk server and ideally a wifi router (although CerboGX or Raspbery Pi can act
//  as an AP for the ESP32).

//  In signalk, configure SK to N2K add-in to forward values to NMEA2000 network if desired, minor bug corrections
//  required for engine paramenters in the add-in

// oil pressure sender sierra OP24301 Range-1 Gauge: 240 Ohms at 0 PSI
// engine coolant sender: american resistance type D
// one wire sensors: DS18B20
// Designed for flywheel gear tooth rpm sensor

// Setup / customization notes:
// - You'll need to know your RPM / fuel flow / boat speed (STW) in calm conditions with clean hull
// - You'll need to estimate the impact of wind and other other factors on engine load
// - You'll need to know your engine rating curve (RPM / kW) for accurate load calculation (ChatGPT can help with this)
//    if you provide the fuel / output curve (diagram) points from your engine's spec sheet
// - You'll need to know how many teeth are on your flywheel gear for RPM calculation
// ============================================================================
// ============================================================================

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

// Custom functions
#include "sender_resistance.h"
#include "engine_hours.h"
#include "calibrated_analog_input.h"
#include "oil_pressure_sender.h"
#include "engine_performance.h"
#include "onewire_sensors.h"
#include "coolant_temp.h"
#include "rpm_sensor.h"


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

// Global SensESP app pointer
using namespace sensesp;
using namespace sensesp::onewire;

// -----------------------------------------------
// FORWARD DECLARATIONS
// -----------------------------------------------

void setup_engine_hours();

// -----------------------------------------------
// PIN DEFINITIONS — FIREBEETLE ESP32-E   EDIT THESE IF REQUIRE FOR YOUR BOARD
// -----------------------------------------------
const uint8_t PIN_TEMP_COMPARTMENT = 4;  // one wire for engine compartment /digital  12
const uint8_t PIN_TEMP_EXHAUST     = 16;  // one wire,strapped to exhaust elbow digital 11
const uint8_t PIN_TEMP_ALT_12V     = 17; // extra sensor... could be aft cabin?? / digital 10 NOT USED

const uint8_t PIN_ADC_COOLANT      = 39;  // engine's coolant temperature sender
const uint8_t PIN_RPM              = 25;   // magnetic pickup for RPM (digital input) digital 2

const uint8_t PIN_ADC_OIL_PRESSURE = 36;   // choose free ADC pin
const float OIL_ADC_REF_VOLTAGE = 2.5f;    // ref voltage of the Firebeetle esp32-e
const float OIL_PULLUP_RESISTOR = 220.0f;   // Resistance (ohm) in mid-range of oil px sender values

// -----------------------------------------------
// CONSTANTS
// -----------------------------------------------
const float ADC_SAMPLE_RATE_HZ = 10.0f;

// From the DFRobot voltage divider specs:
const float DIV_R1 = 30000.0f;     // Top resistor
const float DIV_R2 = 7500.0f;      // Bottom resistor
const float COOLANT_DIVIDER_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;
// COOLANT_DIVIDER_GAIN = 5.0

const float COOLANT_SUPPLY_VOLTAGE = 13.5f;  //13.5 volt nominal, indication will be close enough at 12-14 VDC

// resistance of the coolant gauge located at the helm
const float COOLANT_GAUGE_RESISTOR = 1212.0f;   // Derived from 6.8V @ 1352Ω coolant temp gauge resistance
// recheck this at operating temperature

const float RPM_TEETH = 116.0f;  // Number of teeth on flywheel gear for RPM sender 3JH3E
const float RPM_MULTIPLIER = 1.0f / RPM_TEETH; // to get revolutions per second from pulses per second

const uint32_t ONEWIRE_READ_DELAY_MS = 500;  // half a second between reads

Frequency* g_frequency = nullptr;  // Shared RPM signal (rev/s)
ValueProducer<float>* g_engine_rad_s = nullptr;  // rad/s for Signal K



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

//setup oil pressure sensor
  new OilPressureSender(
      PIN_ADC_OIL_PRESSURE,
      OIL_ADC_REF_VOLTAGE,
      OIL_PULLUP_RESISTOR,
      ADC_SAMPLE_RATE_HZ,
      "propulsion.engine.oilPressure",
      "/config/sensors/oil_pressure"
  );

// Call setup functions for sensors
  setup_temperature_sensors();
  setup_coolant_sender();
  setup_rpm_sensor();
  setup_engine_hours();
  setup_engine_performance(
      g_frequency,
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

  // EngineHours stores the accumulated hours (float, in hours)
  auto* hours = new EngineHours("/config/sensors/engine_hours");

  // Convert hours → seconds for Signal K output
  // 1 hour = 3600 seconds
  auto* hours_to_seconds = hours->connect_to(
      new Linear(3600.0f, 0.0f, "/config/sensors/engine_hours_to_seconds")
  );

  // Signal K output in SECONDS
  auto* sk_hours = new SKOutputFloat(
      "propulsion.engine.runTime",
      "/config/outputs/sk/engine_hours"
  );

  // Connect RPM signal → hours accumulator
  g_frequency->connect_to(hours);

  // Output to SK in seconds
  hours_to_seconds->connect_to(sk_hours);



  // UI configuration remains in HOURS
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
  // OPTIONAL: print pulse count without blocking
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