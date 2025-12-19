// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.0
// ============================================================================
// Features:
//  - Engine Coolant sender via ADC (FireBeetle calibrated) (works in conjuction with gauge)
//  - 3x OneWire temperature sensors
//  - RPM via magnetic pickup (works in conjunction with gauge)
//  - Fuel flow estimation based on RPM (requires obtaining real fuel curve at 2000, 2800 and 3500 RPM)
//  - Engine hours accumulator
//  - Engine load estimation based on RPM and fuel flow
// -  Engine load (%) estimation based on RPM, speed (STW/SOG), wind (requires calibration for your boat)
//  - Engine coolant temp (via boat's coolant temp sender, requires gauge to be fitted  and working, for american sender range)
//  - Fuel flow (LPH) estimation based on RPM and engine load (requires calibration for your engine/boat)
//  - Oil pressure, for american sender resistance (via boat's oil pressure sender, requires gauge to be fitted and working)
//  - Engine hours accumulator, resetable via UI
//  - SK debug output
//  - OTA update
//  - Full UI configuration for SK paths and calibration, setting wifi and SK server address
// values sent to SignalK IAW https://signalk.org/specification/1.5.0/doc/vesselsBranch.html
//  Note: you need a signalk server and ideally a wifi router (although CerboGX can act
//  as AP).
//  In signalk, configure SK to N2K add-in to forward values to NMEA2000 network if desired.)
// values sent to SignalK IAW https://signalk.org/specification/1.5.0/doc/vesselsBranch.html (note: minor errrors
//  in spec (Appendix A) must be corrected for compatibility with canboat.js )
//
//  Note: you need a signalk server and ideally a wifi router (although CerboGX or Raspbery Pi can act
//  as an AP for the ESP32).
//
//  In signalk, configure SK to N2K add-in to forward values to NMEA2000 network if desired, minor bug corrections
//  required for engine paramenters in the add-in
//
// oil pressure sender sierra OP24301 Range-1 Gauge: 240 Ohms at 0 PSI
// engine coolant sender: american resistance type D
// one wire sensors: DS18B20
// Designed for flywheel gear tooth rpm sensor
//
// Setup / customization notes:
// - You'll need to know your RPM / fuel flow / boat speed (STW) in calm conditions with clean hull
// - You'll need to estimate the impact of wind and other other factors on engine load
// - You'll need to know your engine rating curve (RPM / kW) for accurate load calculation (ChatGPT can help with this)
//    if you provide the fuel / output curve (diagram) points from your engine's spec sheet
// - You'll need to know how many teeth are on your flywheel gear for RPM calculation
//
// ============================================================================

#ifndef UNIT_TEST

#include <memory>
#include <cmath>

// ---------------------------------------------------------------------------
// SensESP core
// ---------------------------------------------------------------------------
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/signalk/signalk_value_listener.h"

// Status page (v3.1.0)
#include "sensesp/ui/status_page_item.h"

// Config UI
#include "sensesp/ui/config_item.h"

// Sensors / helpers
#include "sensesp/sensors/analog_input.h"
#include "driver/adc.h"

// Transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/median.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/transforms/moving_average.h"

// Custom modules
#include "sender_resistance.h"
#include "engine_hours.h"
#include "calibrated_analog_input.h"
#include "oil_pressure_sender.h"
#include "engine_performance.h"
#include "onewire_sensors.h"
#include "coolant_temp.h"
#include "rpm_sensor.h"

// Simulators
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

// ---------------------------------------------------------------------------
// FORWARD DECLARATIONS
// ---------------------------------------------------------------------------
void setup_engine_hours();

// ---------------------------------------------------------------------------
// PIN DEFINITIONS — FireBeetle ESP32-E
// ---------------------------------------------------------------------------
const uint8_t PIN_TEMP_COMPARTMENT = 4;
const uint8_t PIN_TEMP_EXHAUST     = 16;
const uint8_t PIN_TEMP_ALT_12V     = 17;

const uint8_t PIN_ADC_COOLANT      = 39;
const uint8_t PIN_RPM              = 25;

const uint8_t PIN_ADC_OIL_PRESSURE = 36;
const float   OIL_ADC_REF_VOLTAGE  = 2.5f;
const float   OIL_PULLUP_RESISTOR  = 220.0f;

// ---------------------------------------------------------------------------
// CONSTANTS — REQUIRED BY SENSOR MODULES (extern in headers)
// ---------------------------------------------------------------------------

// ADC / sampling
const float ADC_SAMPLE_RATE_HZ = 10.0f;

// Coolant divider network
const float DIV_R1 = 30000.0f;
const float DIV_R2 = 7500.0f;
const float COOLANT_DIVIDER_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;

// Coolant electrical model
const float COOLANT_SUPPLY_VOLTAGE = 13.5f;
const float COOLANT_GAUGE_RESISTOR = 1180.0f;

// RPM sensor
const float RPM_TEETH = 116.0f;
const float RPM_MULTIPLIER = 1.0f / RPM_TEETH;

// OneWire timing
const uint32_t ONEWIRE_READ_DELAY_MS = 500;

// Shared RPM frequency (rev/s)
Frequency* g_frequency = nullptr;

// ---------------------------------------------------------------------------
// STATUS PAGE BACKING VARIABLES
// (updated inside modules via `extern`)
// ---------------------------------------------------------------------------

// Coolant
float coolant_adc_volts = NAN;
float coolant_temp_c    = NAN;

// RPM
float rpm_adc_hz = NAN;
float engine_rpm = NAN;

// OneWire temps
float temp_elbow_c       = NAN;
float temp_compartment_c = NAN;
float temp_alternator_c  = NAN;

// Engine performance
float fuel_flow_lph   = NAN;
float engine_load_pct = NAN;

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

  // ========================================================================
  // Engine performance inputs from SK (Signal K paths)
  // ========================================================================
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

  // ========================================================================
  // Oil pressure sender
  // ========================================================================
  new OilPressureSender(
      PIN_ADC_OIL_PRESSURE,
      OIL_ADC_REF_VOLTAGE,
      OIL_PULLUP_RESISTOR,
      ADC_SAMPLE_RATE_HZ,
      "propulsion.engine.oilPressure",
      "/config/sensors/oil_pressure"
  );

  // ========================================================================
  // Module setup
  // ========================================================================
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

  // ========================================================================
  // STATUS PAGE (SensESP v3.1.0)
  // Reads referenced variables at render time
  // ========================================================================

  new StatusPageItem<float>("ADC Voltage (V)",
                            coolant_adc_volts,
                            "Coolant",
                            10);

  new StatusPageItem<float>("Coolant Temp (°C)",
                            coolant_temp_c,
                            "Coolant",
                            20);

  new StatusPageItem<float>("RPM Input Frequency (Hz)",
                            rpm_adc_hz,
                            "Engine",
                            10);

  new StatusPageItem<float>("Engine RPM",
                            engine_rpm,
                            "Engine",
                            20);

  new StatusPageItem<float>("Exhaust Elbow (°C)",
                            temp_elbow_c,
                            "OneWire Temperatures",
                            10);

  new StatusPageItem<float>("Engine Compartment (°C)",
                            temp_compartment_c,
                            "OneWire Temperatures",
                            20);

  new StatusPageItem<float>("Alternator (°C)",
                            temp_alternator_c,
                            "OneWire Temperatures",
                            30);

  new StatusPageItem<float>("Fuel Flow (L/hr)",
                            fuel_flow_lph,
                            "Engine",
                            30);

  new StatusPageItem<float>("Engine Load (%)",
                            engine_load_pct,
                            "Engine",
                            40);

  sensesp_app->start();
}

// ============================================================================
// ENGINE HOURS
// ============================================================================
void setup_engine_hours() {

  auto* hours = new EngineHours("/config/sensors/engine_hours");

  auto* hours_to_seconds = hours->connect_to(
      new Linear(3600.0f, 0.0f)
  );

  auto* sk_hours = new SKOutputFloat(
      "propulsion.engine.runTime"
  );

  if (g_frequency != nullptr) {
    g_frequency->connect_to(hours);
  }

  hours_to_seconds->connect_to(sk_hours);
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  sensesp_app->get_event_loop()->tick();
}

#endif  // UNIT_TEST
