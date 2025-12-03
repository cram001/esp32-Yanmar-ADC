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
// ============================================================================

#include <memory>

// SensESP core
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_output.h"

// Config UI elements
#include "sensesp/ui/config_item.h"

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

using namespace sensesp;
using namespace sensesp::onewire;

// -----------------------------------------------
// FORWARD DECLARATIONS
// -----------------------------------------------
void setup_temperature_sensors();
void setup_coolant_sender();
void setup_rpm_sensor();
void setup_fuel_flow();
void setup_engine_hours();

// -----------------------------------------------
// PIN DEFINITIONS — FIREBEETLE ESP32-E
// -----------------------------------------------
static const uint8_t PIN_TEMP_COMPARTMENT = 4;
static const uint8_t PIN_TEMP_EXHAUST     = 16;
static const uint8_t PIN_TEMP_ALT_12V     = 17;

static const uint8_t PIN_ADC_COOLANT      = 39;
static const uint8_t PIN_RPM              = 25;

// -----------------------------------------------
// CONSTANTS
// -----------------------------------------------
static const float ADC_SAMPLE_RATE_HZ = 10.0f;

static const float DIV_R1 = 250000.0f;
static const float DIV_R2 = 100000.0f;
static const float COOLANT_DIVIDER_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;

static const float COOLANT_SUPPLY_VOLTAGE = 12.0f;
static const float COOLANT_GAUGE_RESISTOR = 1035.0f;

static const float RPM_TEETH = 116.0f;
static const float RPM_MULTIPLIER = 1.0f / RPM_TEETH;

static const uint32_t ONEWIRE_READ_DELAY_MS = 500;

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
// TEMPERATURE SENSORS — FULL UI + EDITABLE SK PATHS
// ============================================================================
void setup_temperature_sensors() {

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
      ->set_description("Temperature of the engine compartment")
      ->set_sort_order(100);

  ConfigItem(t1_linear)
      ->set_title("Engine Room DS18B20 calibration")
      ->set_description("Linear calibration for engine room temperature sensor")
      ->set_sort_order(101);


  ConfigItem(sk_engine)
      ->set_title("Engine Room SK Path")
      ->set_description("Signal K path for engine room temperature")
      ->set_sort_order(102);

  // ========================= EXHAUST ==============================

  auto* t2 = new OneWireTemperature(
      s2, ONEWIRE_READ_DELAY_MS,
      "/config/sensors/temperature/exhaust"
  );

  auto* t2_linear = new Linear(
      1.0, 0.0,
      "/config/sensors/temperature/exhaust/linear"
  );

  auto* sk_exhaust = new SKOutputFloat(
      "propulsion.mainEngine.exhaustTemperature",
      "/config/outputs/sk/exhaust_temp"
  );

  t2->connect_to(t2_linear)->connect_to(sk_exhaust);

  ConfigItem(t2)
    ->set_title("Exhaust DS18B20")
    ->set_description("Temperature of the engine exhaust elbow")
    ->set_sort_order(200);

  ConfigItem(t2_linear)
    ->set_title("Exhaust Temperature Calibration")
    ->set_description("Linear calibration for exhaust temperature sensor")
    ->set_sort_order(201);

  ConfigItem(sk_exhaust)
      ->set_title("Exhaust SK Path")
      ->set_description("Signal K path for exhaust temperature")
      ->set_sort_order(202);




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
    ->set_title("Alternator DS18B20")
    ->set_description("Temperature of the alternator housing")
    ->set_sort_order(300);

  ConfigItem(t3_linear)
    ->set_title("Alternator Temperature Calibration")
    ->set_description("Linear calibration for alternator temperature sensor")
    ->set_sort_order(301);

  ConfigItem(sk_alt)
    ->set_title("Alternator SK Path")
    ->set_description("Signal K path for alternator temperature")
    ->set_sort_order(302);
}

// ============================================================================
// COOLANT SENDER — UI + EDITABLE SK PATH
// ============================================================================
void setup_coolant_sender() {

  auto* adc_raw = new AnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/config/sensors/coolant/adc_raw"
  );

  auto* volts = adc_raw->connect_to(
      new Linear(0.000001067f, 0.0f,
                 "/config/sensors/coolant/voltage")
  );

  auto* v_corrected = volts->connect_to(
      new Linear(COOLANT_DIVIDER_GAIN, 0.0f,
                 "/config/sensors/coolant/corrected_voltage")
  );

  auto* v_smooth = v_corrected->connect_to(
      new Median(5, "/config/sensors/coolant/median")
  );

  auto* sender_res = v_smooth->connect_to(
      new SenderResistance(COOLANT_SUPPLY_VOLTAGE, COOLANT_GAUGE_RESISTOR)
  );

  // Temp curve ohms to °C
  std::set<CurveInterpolator::Sample> ohms_to_temp = {
      {29.6f, 121.0f}, {45.0f, 100.0f}, {85.5f, 85.0f}, {90.9f, 82.5f},
      {97.0f, 80.0f}, {104.0f, 76.7f}, {112.0f, 72.0f}, {131.0f, 63.9f},
      {207.0f, 56.0f}, {1352.0f, 12.7f}
  };

  auto* temp_C = sender_res->connect_to(
      new CurveInterpolator(&ohms_to_temp,
                            "/config/sensors/coolant/temp_curve")
  );

  auto* sk_coolant = new SKOutputFloat(
      "propulsion.mainEngine.coolantTemperature", 
      "/config/outputs/sk/coolant_temp"
  );

  temp_C->connect_to(sk_coolant);

  ConfigItem(sk_coolant)
      ->set_title("Coolant Temperature SK Path")
      ->set_description("Signal K path for engine coolant temperature Ohm to °C curve")
      ->set_sort_order(400);

  ConfigItem(temp_C)
      ->set_title("Coolant Temperature Curve")
      ->set_description("Curve interpolator for coolant temperature sensor ")
      ->set_sort_order(401);


  // DEBUG OUTPUTS
  volts->connect_to(new SKOutputFloat("debug.coolant.volts"));
  v_corrected->connect_to(new SKOutputFloat("debug.coolant.correctedVoltage"));
  sender_res->connect_to(new SKOutputFloat("debug.coolant.senderResistance"));
  temp_C->connect_to(new SKOutputFloat("debug.coolant.temperature"));
}

// ============================================================================
// RPM SENSOR — UI + EDITABLE SK PATH
// ============================================================================
void setup_rpm_sensor() {

  auto* rpm_input =
      new DigitalInputCounter(PIN_RPM, INPUT_PULLUP, RISING, 100);

  g_frequency = new Frequency(RPM_MULTIPLIER,
                              "/config/sensors/rpm/frequency");

  auto* sk_rpm = new SKOutputFloat(
      "propulsion.mainEngine.revolutions",
      "/config/outputs/sk/rpm"
  );

  rpm_input->connect_to(g_frequency)->connect_to(sk_rpm);

 
  ConfigItem(sk_rpm)
      ->set_title("RPM SK Path")
      ->set_description("Signal K path for engine RPM")
      ->set_sort_order(500);

  g_frequency->connect_to(new SKOutputFloat("debug.rpm"));
}

// ============================================================================
// FUEL FLOW — UI + EDITABLE SK PATH  engine RPM to LPH curve
// ============================================================================
void setup_fuel_flow() {

  std::set<CurveInterpolator::Sample> rpm_to_lph = {
      {500, 0.6f}, {850, 0.7f}, {1500, 1.8f}, {2000, 2.3f},
      {2800, 2.7f}, {3000, 3.0f}, {3400, 4.5f}, {3800, 6.0f}
  };

  auto* fuel_lph = g_frequency->connect_to(
      new CurveInterpolator(&rpm_to_lph,
                            "/config/sensors/fuel/lph_curve")
  );

  auto* fuel_m3 = fuel_lph->connect_to(
      new Linear(0.001f, 0.0f,
                 "/config/sensors/fuel/m3")
  );

  auto* sk_fuel = new SKOutputFloat(
      "propulsion.mainEngine.fuel.rate",
      "/config/outputs/sk/fuel_rate"
  );

  fuel_m3->connect_to(sk_fuel);

  ConfigItem(fuel_lph)->set_title("Fuel Flow Curve")
      ->set_description("Curve interpolator for fuel flow based on RPM RPM to LPH curve")
      ->set_sort_order(600);

  ConfigItem(sk_fuel)->set_title("Fuel Rate SK Path")
      ->set_description("Signal K path for engine fuel rate")
      ->set_sort_order(601);
    

  fuel_lph->connect_to(new SKOutputFloat("debug.fuel.lph"));
}

// ============================================================================
// ENGINE HOURS — UI + EDITABLE SK PATH
// ============================================================================
void setup_engine_hours() {

  auto* hours = new EngineHours("/config/sensors/engine_hours");

  auto* sk_hours = new SKOutputFloat(
      "propulsion.mainEngine.runTime",
      "/config/outputs/sk/engine_hours"
  );

  g_frequency->connect_to(hours);

  hours->connect_to(sk_hours);
  hours->connect_to(new SKOutputFloat("debug.engineHours"));

  ConfigItem(hours)->set_title("Engine Hours Accumulator");
  ConfigItem(sk_hours)->set_title("Engine Hours SK Path");
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  sensesp_app->get_event_loop()->tick();
}

