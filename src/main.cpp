// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.1
// ============================================================================
// Features:
//  - Coolant sender via ADC (FireBeetle calibrated)
//  - 3x OneWire temperature sensors
//  - RPM via magnetic pickup
//  - Fuel flow estimation
//  - Engine hours accumulator
//  - SK debug output
//  - OTA update
// ============================================================================

#include <memory>

// SensESP core
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_output.h"

// Sensors
#include "sensesp/sensors/analog_input.h"
#include "sensesp/sensors/digital_input.h"
#include "sensesp_onewire/onewire_temperature.h"

// Transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/median.h"
#include "sensesp/transforms/frequency.h"
#include "sensesp/transforms/curveinterpolator.h"

// Custom transforms
#include "sender_resistance.h"
#include "engine_hours.h"
#include "debug_value.h"

using namespace sensesp;
using namespace sensesp::onewire;

// ============================================================================
// FORWARD DECLARATIONS  (Required for C++ compilation order)
// ============================================================================
void setup_temperature_sensors();
void setup_coolant_sender();
void setup_rpm_sensor();
void setup_fuel_flow();
void setup_engine_hours();

// ============================================================================
// PIN DEFINITIONS — FIREBEETLE ESP32-E
// ============================================================================
static const uint8_t PIN_TEMP_COMPARTMENT = 4;
static const uint8_t PIN_TEMP_EXHAUST     = 16;
static const uint8_t PIN_TEMP_ALT_12V     = 17;

static const uint8_t PIN_ADC_COOLANT      = 39;   // ADC1
static const uint8_t PIN_RPM              = 25;   // PCNT-capable

// ============================================================================
// CONSTANTS
// ============================================================================
static const float ADC_SAMPLE_RATE_HZ     = 10.0f;

// Voltage divider used on coolant sender
static const float DIV_R1 = 250000.0f;
static const float DIV_R2 = 100000.0f;
static const float COOLANT_DIVIDER_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;

// Coolant sender system parameters
static const float COOLANT_SUPPLY_VOLTAGE = 12.0f;
static const float COOLANT_GAUGE_RESISTOR = 1035.0f;

static const float RPM_TEETH = 116.0f;
static const float RPM_MULTIPLIER = 1.0f / RPM_TEETH;

// OneWire polling
static const uint32_t ONEWIRE_READ_DELAY_MS = 500;

// Globals
Frequency* g_frequency = nullptr;

// ============================================================================
// SETUP
// ============================================================================
void setup() {

  SetupLogging();

  SensESPAppBuilder builder;

  builder.set_hostname("esp32-yanmar")
      ->set_wifi_client("wifi_ssid", "wifi_passphrase")
      ->enable_ota("esp32!")
      ->set_sk_server("192.168.150.95", 3000);

  sensesp_app = builder.get_app();

  setup_temperature_sensors();
  setup_coolant_sender();
  setup_rpm_sensor();
  setup_fuel_flow();
  setup_engine_hours();
}

// ============================================================================
// TEMPERATURE SENSORS
// ============================================================================
void setup_temperature_sensors() {

  auto* s1 = new DallasTemperatureSensors(PIN_TEMP_COMPARTMENT);
  auto* s2 = new DallasTemperatureSensors(PIN_TEMP_EXHAUST);
  auto* s3 = new DallasTemperatureSensors(PIN_TEMP_ALT_12V);

  auto* t1 = new OneWireTemperature(s1, ONEWIRE_READ_DELAY_MS, "/t/engine");
  t1->connect_to(new Linear(1.0, 0.0))
     ->connect_to(new SKOutputFloat("environment.inside.engineRoom.temperature"));

  auto* t2 = new OneWireTemperature(s2, ONEWIRE_READ_DELAY_MS, "/t/exhaust");
  t2->connect_to(new Linear(1.0, 0.0))
     ->connect_to(new SKOutputFloat("propulsion.mainEngine.exhaustTemperature"));

  auto* t3 = new OneWireTemperature(s3, ONEWIRE_READ_DELAY_MS, "/t/alt");
  t3->connect_to(new Linear(1.0, 0.0))
     ->connect_to(new SKOutputFloat("electrical.alternators.main.temperature"));
}

// ============================================================================
// COOLANT SENDER — FireBeetle ADC calibration applied
// ============================================================================
// ADC raw → volts (scaled) → divider correction → median → resistance → temp
// ============================================================================
void setup_coolant_sender() {

  // 1. ADC RAW INPUT (FireBeetle returns oversampled raw counts)
  auto* adc_raw = new AnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/coolant/adc_raw"
  );

  // 2. Convert raw ADC → actual volts
  //
  // Calibration constant determined experimentally:
  //
  //      volts = raw_adc × 0.000001067
  //
  auto* volts = adc_raw->connect_to(
      new Linear(0.000001067f, 0.0f, "/coolant/volts")
  );

  // 3. Undo voltage divider
  auto* v_corrected = volts->connect_to(
      new Linear(COOLANT_DIVIDER_GAIN, 0.0f, "/coolant/corrected")
  );

  // 4. Smooth noise with median filter
  auto* v_smooth = v_corrected->connect_to(
      new Median(5, "/coolant/median")
  );

  // 5. Convert sensed voltage → resistance of the coolant sender
  auto* sender_res = v_smooth->connect_to(
      new SenderResistance(COOLANT_SUPPLY_VOLTAGE, COOLANT_GAUGE_RESISTOR)
  );

  // 6. Convert resistance → temperature (°C)
std::set<CurveInterpolator::Sample> ohms_to_temp = {
    {29.6f, 121.0f},
    {45.0f, 100.0f},
    {85.5f, 85.0f},
    {90.9f, 82.5f},
    {97.0f, 80.0f},
    {104.0f, 76.7f},
    {112.0f, 72.0f},
    {131.0f, 63.9f},
    {207.0f, 56.0f},
    {1352.0f, 12.7f}
};


  auto* temp_C = sender_res->connect_to(
      new CurveInterpolator(&ohms_to_temp, "/coolant/temp_curve")
  );

// 7. Convert °C → Kelvin for SK-compliant output
auto* temp_K = temp_C->connect_to(
      new Linear(1.0f, 273.15f, "/coolant/temp_K")
  );

temp_K->connect_to(
      new SKOutputFloat("propulsion.mainEngine.coolantTemperature")
  );

  // DEBUG OUTPUTS
  volts->connect_to(new SKOutputFloat("debug.coolant.volts"));
  v_corrected->connect_to(new SKOutputFloat("debug.coolant.correctedVoltage"));
  sender_res->connect_to(new SKOutputFloat("debug.coolant.senderResistance"));
  temp_C->connect_to(new SKOutputFloat("debug.coolant.temperature"));
}

// ============================================================================
// RPM SENSOR
// ============================================================================
void setup_rpm_sensor() {

  auto* rpm_input =
      new DigitalInputCounter(PIN_RPM, INPUT_PULLUP, RISING, 100);

  g_frequency = new Frequency(RPM_MULTIPLIER, "/rpm/cal");

  rpm_input->connect_to(g_frequency)
           ->connect_to(new SKOutputFloat("propulsion.mainEngine.revolutions"));

  g_frequency->connect_to(new SKOutputFloat("debug.rpm"));
}

// ============================================================================
// FUEL FLOW
// ============================================================================
void setup_fuel_flow() {

  std::set<CurveInterpolator::Sample> rpm_to_lph = {
      {500, 0.6f},
      {850, 0.7f},
      {1500, 1.8f},
      {2000, 2.3f},
      {2800, 2.7f},
      {3000, 3.0f},
      {3400, 4.5f},
      {3800, 6.0f}
  };

  auto* fuel_lph = g_frequency->connect_to(
      new CurveInterpolator(&rpm_to_lph, "/fuel/lph_curve")
  );

  auto* fuel_m3 = fuel_lph->connect_to(
      new Linear(0.001f, 0.0f, "/fuel/m3")
  );

  fuel_m3->connect_to(
      new SKOutputFloat("propulsion.mainEngine.fuel.rate")
  );

  fuel_lph->connect_to(new SKOutputFloat("debug.fuel.lph"));
}

// ============================================================================
// ENGINE HOURS
// ============================================================================
void setup_engine_hours() {

  auto* hours = new EngineHours("/engine/hours");

  g_frequency->connect_to(hours);

  hours->connect_to(new SKOutputFloat("propulsion.mainEngine.runTime"));
  hours->connect_to(new SKOutputFloat("debug.engineHours"));
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  sensesp_app->get_event_loop()->tick();
}