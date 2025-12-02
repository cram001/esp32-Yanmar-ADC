// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.1
// ============================================================================
// Features:
//  - 3x OneWire temperature sensors (each with seperate esp32 pin)
//  - Coolant sender temperature via ADC + divider + smoothing + curve interpolation
//  - RPM via magnetic pickup
//  - Fuel flow estimation via RPM → LPH → m³/hr (for Yanmar3JH3E with 17x13 RH 3 blade prop)
//  - Engine hours accumulator (UI-editable initial hours)
//  - Debug values sent to Signal K
// Web UI for config (wifi, SignalK server, etc...)
// 4MB flash required to enable OTA update (min). 8MB or more recommended for future expansion.
// ============================================================================

#include <memory>

// SensESP core
#include "sensesp/signalk/signalk_output.h"
#include "sensesp_app_builder.h"

// UI
#include "sensesp/ui/config_item.h"

// Sensors
#include "sensesp/sensors/analog_input.h"
#include "sensesp/sensors/digital_input.h"
#include "sensesp_onewire/onewire_temperature.h"

// Transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/transforms/frequency.h"
#include "sensesp/transforms/median.h"

// Custom transforms
#include "sender_resistance.h"
#include "engine_hours.h"
#include "debug_value.h"

using namespace sensesp;
using namespace sensesp::onewire;


// ============================================================================
// USER CONFIGURABLE CONSTANTS, select GPIO pins carefully!
// ============================================================================

// OneWire pins
static const uint8_t PIN_TEMP_COMPARTMENT = 4;
static const uint8_t PIN_TEMP_EXHAUST     = 16;
static const uint8_t PIN_TEMP_ALT_12V     = 17;

// ADC pin for coolant sender
static const uint8_t PIN_ADC_COOLANT = 34;

// RPM magnetic pickup
static const uint8_t PIN_RPM  = 25;
static const float   RPM_TEETH = 116.0f;   // Yanmar 3JH3E ring gear has 116 teeth

// Voltage divider for coolant sender
static const float DIV_R1 = 250000.0f;    //R1 voltage divider resistor (ohms)
static const float DIV_R2 = 100000.0f;    //R2 voltage divider resistor (ohms)
static const float DIV_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;

// Sender resistance calculation
static const float COOLANT_SUPPLY_VOLTAGE = 12.0f;
static const float COOLANT_GAUGE_RESISTOR = 1035.0f;

// OneWire sample rate
static const uint32_t ONEWIRE_READ_DELAY_MS = 500;


// SETUP

static const float ADC_SAMPLE_RATE_HZ = 10.0f;
static const float COOLANT_DIVIDER_GAIN = DIV_GAIN;   // consistent name
static const float RPM_MULTIPLIER = 1.0f / RPM_TEETH;


// ============================================================================
//                           FORWARD DECLARATIONS
// ============================================================================

void setup_temperature_sensors();
void setup_coolant_sender();
void setup_rpm_sensor();
void setup_fuel_flow();
void setup_engine_hours();
void setup_debug_panel();


// ============================================================================
//                                    SETUP
// ============================================================================
void setup() {

  SetupLogging();

  SensESPAppBuilder builder;

  builder.set_hostname("esp32-yanmar")
      ->set_wifi_client("wifi_ssid", "wifi_passphrase")  //change your network info here
      ->enable_ota("esp32!")  //OTA connect password (notfor wifi)
      ->set_sk_server("192.168.150.95", 3000); //change to your Signal K server address

  sensesp_app = builder.get_app();

  setup_temperature_sensors();
  setup_coolant_sender();
  setup_rpm_sensor();
  setup_fuel_flow();
  setup_engine_hours();
  setup_debug_panel();
}


// ============================================================================
//                              TEMPERATURE SENSORS
// ============================================================================
void setup_temperature_sensors() {

  auto* dts1 = new DallasTemperatureSensors(PIN_TEMP_COMPARTMENT);
  auto* dts2 = new DallasTemperatureSensors(PIN_TEMP_EXHAUST);
  auto* dts3 = new DallasTemperatureSensors(PIN_TEMP_ALT_12V);

  // ENGINE COMPARTMENT TEMP
  auto* t_engine =
      new OneWireTemperature(dts1, ONEWIRE_READ_DELAY_MS, "/t/engine");

  auto* t_engine_cal =
      new Linear(1.0f, 0.0f, "/t/engine/linear");

  auto* t_engine_sk =
      new SKOutputFloat("environment.inside.engineRoom.temperature",
                        "/t/engine/sk");

  ConfigItem(t_engine)->set_title("Engine Compartment Temp");
  t_engine->connect_to(t_engine_cal)->connect_to(t_engine_sk);

  // EXHAUST TEMP
  auto* t_exhaust =
      new OneWireTemperature(dts2, ONEWIRE_READ_DELAY_MS, "/t/exhaust");

  auto* t_exhaust_cal =
      new Linear(1.0f, 0.0f, "/t/exhaust/linear");

  auto* t_exhaust_sk =
      new SKOutputFloat("propulsion.mainEngine.exhaustTemperature",
                        "/t/exhaust/sk");

  ConfigItem(t_exhaust)->set_title("Exhaust Temp");
  t_exhaust->connect_to(t_exhaust_cal)->connect_to(t_exhaust_sk);

  // ALTERNATOR TEMP
  auto* t_alt =
      new OneWireTemperature(dts3, ONEWIRE_READ_DELAY_MS, "/t/alt");

  auto* t_alt_cal =
      new Linear(1.0f, 0.0f, "/t/alt/linear");

  auto* t_alt_sk =
      new SKOutputFloat("electrical.alternators.main.temperature",
                        "/t/alt/sk");

  ConfigItem(t_alt)->set_title("12V Alternator Temp");
  t_alt->connect_to(t_alt_cal)->connect_to(t_alt_sk);
}


// ============================================================================
//                              COOLANT SENDER
// ============================================================================
void setup_coolant_sender() {

  // Raw ADC voltage input (0–3.3 V)
  auto* adc_input = new AnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/coolant/adc",
      3.3       // ADC ---> Volts (VS2 method)
  );

  // Undo divider
  auto* v_corrected =
      adc_input->connect_to(
          new Linear(COOLANT_DIVIDER_GAIN, 0.0f, "/coolant/divider")
      );

  // Smoothing
  auto* v_smooth =
      v_corrected->connect_to(
          new Median(5, "/coolant/median")
      );

  // Convert to sender resistance (Ohms)
  auto* sender_res =
      v_smooth->connect_to(
          new SenderResistance(COOLANT_SUPPLY_VOLTAGE,
                               COOLANT_GAUGE_RESISTOR)
                            
      );

  // Ohms → °C curve
  std::set<CurveInterpolator::Sample> r_to_temp{
      { -1.0f,  0.0f },
      {1352.0f, 12.7f},
      {207.0f,  56.0f},
      {131.0f,  63.9f},
      {112.0f,  72.0f},
      {104.0f,  76.7f},
      { 97.0f,  80.0f},
      { 90.9f, 82.5f},
      { 85.5f, 85.0f},
      { 45.0f, 100.0f},
      { 29.6f, 121.0f}
  };

  auto* temp_c =
      sender_res->connect_to(
          new CurveInterpolator(&r_to_temp, "/coolant/curve")
      );

  auto* temp_sk =
      new SKOutputFloat("propulsion.mainEngine.coolantTemperature",
                        "/coolant/sk");

  temp_c->connect_to(temp_sk);
}


// ============================================================================
//                                RPM SENSOR
// ============================================================================
Frequency* g_frequency = nullptr;

void setup_rpm_sensor() {

  auto* rpm_sensor =
      new DigitalInputCounter(PIN_RPM, INPUT_PULLUP, RISING, ONEWIRE_READ_DELAY_MS);

  g_frequency = new Frequency(RPM_MULTIPLIER, "/rpm/cal");

  auto* rpm_sk =
      new SKOutputFloat("propulsion.mainEngine.revolutions", "/rpm/sk");

  rpm_sensor->connect_to(g_frequency)->connect_to(rpm_sk);
}


// ============================================================================
//                                FUEL FLOW
// ============================================================================
void setup_fuel_flow() {

  std::set<CurveInterpolator::Sample> rpm_to_fuel{
      {500, 0.6f},
      {850, 0.7f},
      {1500, 1.8f},
      {2000, 2.3f},
      {2800, 2.7f},
      {3000, 3.0f},
      {3400, 4.5f},
      {3800, 6.0f}
  };

  auto* fuel_lph =
      g_frequency->connect_to(
          new CurveInterpolator(&rpm_to_fuel, "/fuel/curve")
      );

  auto* fuel_m3 =
      fuel_lph->connect_to(new Linear(0.001f, 0.0f, "/fuel/m3"));

  auto* sk =
      new SKOutputFloat("propulsion.mainEngine.fuel.rate", "/fuel/sk");

  fuel_m3->connect_to(sk);
}


// ============================================================================
//                              ENGINE HOURS
// ============================================================================
void setup_engine_hours() {

  auto* engine_hours = new EngineHours("/engine/hours");

  ConfigItem(engine_hours)
      ->set_title("Engine Hours")
      ->set_description("User-editable. Increases when RPM > 500.")
      ->set_sort_order(1300);

  g_frequency->connect_to(engine_hours);

  auto* sk =
      new SKOutputFloat("propulsion.mainEngine.runTime", "/engine/hours/sk");

  engine_hours->connect_to(sk);
}


// ============================================================================
//                                DEBUG UI
// ============================================================================
void setup_debug_panel() {

  // Debug ADC voltage (corrected)
  auto* dbg_adc =
      new DebugValue("ADC Corrected", "/debug/adc");

  // Debug sender resistance
  auto* dbg_res =
      new DebugValue("Sender Resistance", "/debug/resistance");

  // Debug coolant temp
  auto* dbg_temp =
      new DebugValue("Coolant Temp", "/debug/temp");

  // Debug RPM
  auto* dbg_rpm =
      new DebugValue("RPM", "/debug/rpm");

  // Debug Hours
  auto* dbg_hours =
      new DebugValue("Engine Hours", "/debug/hours");
}


// ============================================================================
//                                  LOOP
// ============================================================================
void loop() {
  sensesp_app->get_event_loop()->tick();
}
