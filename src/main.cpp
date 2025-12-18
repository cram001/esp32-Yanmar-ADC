// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.0
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
// CONSTANTS
// ---------------------------------------------------------------------------
const float ADC_SAMPLE_RATE_HZ = 10.0f;

const float DIV_R1 = 30000.0f;
const float DIV_R2 = 7500.0f;
const float COOLANT_DIVIDER_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;

const float COOLANT_SUPPLY_VOLTAGE = 13.5f;
const float COOLANT_GAUGE_RESISTOR = 1180.0f;

const float RPM_TEETH = 116.0f;
const float RPM_MULTIPLIER = 1.0f / RPM_TEETH;

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
  sensesp_app->start();

  // ========================================================================
  // STATUS PAGE (SensESP v3.1.0)
  // URL: http://<esp32-ip>/status
  // ========================================================================

  // ---- Coolant ------------------------------------------------------------
  new StatusPageItem<float>("ADC Voltage (V)",
                            coolant_adc_volts,
                            "Coolant",
                            10);

  new StatusPageItem<float>("Coolant Temp (°C)",
                            coolant_temp_c,
                            "Coolant",
                            20);

  // ---- Engine Speed -------------------------------------------------------
  new StatusPageItem<float>("RPM Input Frequency (Hz)",
                            rpm_adc_hz,
                            "Engine",
                            10);

  new StatusPageItem<float>("Engine RPM",
                            engine_rpm,
                            "Engine",
                            20);

  // ---- Temperatures -------------------------------------------------------
  new StatusPageItem<float>("Exhaust Elbow (°C)",
                            temp_elbow_c,
                            "OneWireTemperatures",
                            10);

  new StatusPageItem<float>("Engine Compartment (°C)",
                            temp_compartment_c,
                            "OneWire Temperatures",
                            20);

  new StatusPageItem<float>("Alternator (°C)",
                            temp_alternator_c,
                            "OneWire Temperatures",
                            30);

  // ---- Engine Performance -------------------------------------------------
  new StatusPageItem<float>("Fuel Flow (L/hr)",
                            fuel_flow_lph,
                            "Engine",
                            10);

  new StatusPageItem<float>("Engine Load (%)",
                            engine_load_pct,
                            "Engine",
                            20);

  // ========================================================================
  // Engine performance inputs from SK (Signal K paths)
  /* 
  Signal K Path	Unit	Notes
navigation.speedThroughWater	m/s	SI normalized
navigation.speedOverGround	m/s	SI normalized
environment.wind.speedApparent	m/s	SI normalized
environment.wind.angleApparent	radians	Signed, vessel-relative
*/
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
}

// ============================================================================
// ENGINE HOURS
// ============================================================================
void setup_engine_hours() {

  auto* hours = new EngineHours("/config/sensors/engine_hours");

  auto* hours_to_seconds = hours->connect_to(
      new Linear(3600.0f, 0.0f, "/config/sensors/engine_hours_to_seconds")
  );

  auto* sk_hours = new SKOutputFloat(
      "propulsion.engine.runTime",
      "/config/outputs/sk/engine_hours"
  );

  if (g_frequency != nullptr) {
    g_frequency->connect_to(hours);
  }

  hours_to_seconds->connect_to(sk_hours);

#if ENABLE_DEBUG_OUTPUTS
  hours->connect_to(new SKOutputFloat("debug.engineHours"));
#endif

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
