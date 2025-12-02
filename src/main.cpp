// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.1
// ============================================================================
// Features:
//  - 3x OneWire temperature sensors (each with seperate esp32 pin)
//  - Coolant sender temperature via ADC + divider + smoothing + curve interpolation
//  - RPM via magnetic pickup
//  - Fuel flow estimation via RPM → LPH → m³/hr
//  - Engine hours accumulator (UI-editable initial hours)
//  - Debug values sent to Signal K
// Web UI for config (wifi, SignalK server, etc...)
//
// Fully SensESP v3.1.1 compliant
// - ConfigItem only applied to Serializable + Saveable types
// - Clean helper functions
// - Sorted UI ordering (Option B: 10-step spacing)
// ============================================================================

#include <memory>

// SensESP Core
#include "sensesp/signalk/signalk_output.h"
#include "sensesp_app_builder.h"
#include "sensesp/ui/config_item.h"

// Sensors
#include "sensesp_onewire/onewire_temperature.h"
#include "sensesp/sensors/analog_input.h"
#include "sensesp/sensors/digital_input.h"

// Transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/median.h"
#include "sensesp/transforms/frequency.h"
#include "sensesp/transforms/curveinterpolator.h"

// Custom modules
#include "engine_hours.h"
#include "sender_resistance.h"
#include "debug_value.h"

using namespace sensesp;
using namespace sensesp::onewire;

extern "C" {
  #include "esp_partition.h"
  #include "esp_ota_ops.h"
}



// ============================================================================
// USER CONFIGURABLE CONSTANTS
// ============================================================================

// OneWire pins
static const uint8_t PIN_TEMP_COMPARTMENT = 4;
static const uint8_t PIN_TEMP_EXHAUST     = 16;
static const uint8_t PIN_TEMP_ALT_12V     = 12;

// ADC pin for coolant sender
static const uint8_t PIN_ADC_COOLANT = 34;

// RPM magnetic pickup
static const uint8_t PIN_RPM  = 25;
static const float   RPM_TEETH = 116.0f;

// Voltage divider for coolant sender
static const float DIV_R1 = 250000.0f;
static const float DIV_R2 = 100000.0f;
static const float DIV_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;

// Coolant sender characteristics
static const float COOLANT_SUPPLY_VOLT   = 12.0f;
static const float COOLANT_GAUGE_RESIST  = 1035.0f;

// Read delays
static const uint32_t ONEWIRE_READ_DELAY = 500;  // ms
static const float    ADC_SAMPLE_RATE_HZ = 10.0; // Hz

// Shared references
static Frequency* rpm_frequency = nullptr;
static FloatTransform* coolant_temp_c = nullptr;

// ============================================================================
// FORWARD DECLARATIONS d
// ============================================================================
void setup_temperature_sensors();
void setup_rpm_sensor();
void setup_coolant_sender();
void setup_fuel_flow();
void setup_engine_hours();
void setup_debug_sk();

void dump_partition_info() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    ESP_LOGI("PART", "Running partition:");
    ESP_LOGI("PART", "  Label:   %s", running->label);
    ESP_LOGI("PART", "  Subtype: 0x%02X", running->subtype);
    ESP_LOGI("PART", "  Offset:  0x%X", running->address);
    ESP_LOGI("PART", "  Size:    %d bytes", running->size);
}

// ============================================================================
// setup()
// ============================================================================
void setup() {

  SetupLogging();

  SensESPAppBuilder builder;

  builder.set_hostname("esp32-yanmar")
      ->set_wifi_client("papasmurf2", "2flyornot2fly")
      ->enable_ota("bateau1!")
      ->set_sk_server("192.168.150.95", 3000);

  sensesp_app = builder.get_app();

  setup_temperature_sensors();
  setup_rpm_sensor();
  setup_coolant_sender();
  setup_fuel_flow();
  setup_engine_hours();
  setup_debug_sk();

   dump_partition_info();

}

// ============================================================================
// TEMPERATURE SENSORS
// ============================================================================
void setup_temperature_sensors() {

  auto* dts1 = new DallasTemperatureSensors(PIN_TEMP_COMPARTMENT);
  auto* dts2 = new DallasTemperatureSensors(PIN_TEMP_EXHAUST);
  auto* dts3 = new DallasTemperatureSensors(PIN_TEMP_ALT_12V);

  // --- Engine compartment (110–119) ---
  auto* t_comp = new OneWireTemperature(dts1, ONEWIRE_READ_DELAY, "/temp/comp");
  ConfigItem(t_comp)
      ->set_title("Engine Compartment Temp")
      ->set_description("OneWire sensor")
      ->set_sort_order(110);

  auto* t_comp_adj = new Linear(1.0, 0.0, "/temp/comp/lin");
  auto* t_comp_sk =
      new SKOutputFloat("environment.inside.engineRoom.temperature",
                        "/temp/comp/sk");
  ConfigItem(t_comp_sk)
      ->set_title("Compartment Temp SK Path")
      ->set_sort_order(111);

  t_comp->connect_to(t_comp_adj)->connect_to(t_comp_sk);

  // --- Exhaust elbow (120–129) ---
  auto* t_exh = new OneWireTemperature(dts2, ONEWIRE_READ_DELAY, "/temp/exh");
  ConfigItem(t_exh)
      ->set_title("Exhaust Elbow Temp")
      ->set_sort_order(120);

  auto* t_exh_adj = new Linear(1.0, 0.0, "/temp/exh/lin");
  auto* t_exh_sk =
      new SKOutputFloat("propulsion.mainEngine.exhaustTemperature",
                        "/temp/exh/sk");
  ConfigItem(t_exh_sk)->set_title("Exhaust Temp SK Path")->set_sort_order(121);

  t_exh->connect_to(t_exh_adj)->connect_to(t_exh_sk);

  // --- Alternator (130–139) ---
  auto* t_alt = new OneWireTemperature(dts3, ONEWIRE_READ_DELAY, "/temp/alt");
  ConfigItem(t_alt)
      ->set_title("12V Alternator Temp")
      ->set_sort_order(130);

  auto* t_alt_adj = new Linear(1.0, 0.0, "/temp/alt/lin");
  auto* t_alt_sk =
      new SKOutputFloat("electrical.alternators.mainengine.temperature",
                        "/temp/alt/sk");
  ConfigItem(t_alt_sk)->set_title("Alt Temp SK Path")->set_sort_order(131);

  t_alt->connect_to(t_alt_adj)->connect_to(t_alt_sk);
}

// ============================================================================
// RPM SENSOR
// ============================================================================
void setup_rpm_sensor() {

  auto* rpm_counter =
      new DigitalInputCounter(PIN_RPM, INPUT_PULLUP, RISING, ONEWIRE_READ_DELAY);

  float multiplier = 1.0f / RPM_TEETH;

  rpm_frequency = new Frequency(multiplier, "/rpm/cal");

  auto* rpm_sk =
      new SKOutputFloat("propulsion.mainEngine.revolutions", "/rpm/sk");

  ConfigItem(rpm_sk)
      ->set_title("RPM SK Path")
      ->set_sort_order(210);

  rpm_counter->connect_to(rpm_frequency)->connect_to(rpm_sk);
}

// ============================================================================
// COOLANT SENDER
// ============================================================================
void setup_coolant_sender() {

  auto* adc = new AnalogInput(PIN_ADC_COOLANT, ADC_SAMPLE_RATE_HZ,
                              "/coolant/adc", 3.3f);
  ConfigItem(adc)
      ->set_title("Coolant ADC Input")
      ->set_sort_order(310);

  auto* scaled =
      adc->connect_to(new Linear(DIV_GAIN, 0.0, "/coolant/scaled"));

  auto* median =
      scaled->connect_to(new Median(5, "/coolant/median"));

  auto* sender_res =
      median->connect_to(new SenderResistance(COOLANT_SUPPLY_VOLT,
                                              COOLANT_GAUGE_RESIST));

// Coolant temperature curve {Resistance (ohms), Temperature (°C)}, set 
// for American sender 450-30 ohm

  std::set<CurveInterpolator::Sample> r_to_temp{
      {-1,0}, {1352,12.7}, {207,56}, {131,63.9}, {88,70},
      {112,72}, {104,76.7}, {97,80}, {90.9,82.5}, {85.5,85},
      {45,100}, {29.6,121}
  };

  coolant_temp_c =
      sender_res->connect_to(new CurveInterpolator(&r_to_temp,
                                                   "/coolant/curve"));

  auto* sk_out =
      new SKOutputFloat("propulsion.mainEngine.coolantTemperature",
                        "/coolant/sk");
  ConfigItem(sk_out)
      ->set_title("Coolant Temp SK Path")
      ->set_sort_order(320);

  coolant_temp_c->connect_to(sk_out);
}

// ============================================================================
// FUEL FLOW
// ============================================================================
void setup_fuel_flow() {

// RPM to LPH curve {RPM, LPH}

  std::set<CurveInterpolator::Sample> rpm_to_lph{
      {500,0.6}, {850,0.7}, {1500,1.8}, {2000,2.3},
      {2800,2.7}, {3000,3.0}, {3400,4.5}, {3800,6.0}
  };

  auto* fuel_lph =
      rpm_frequency->connect_to(new CurveInterpolator(&rpm_to_lph,
                                                       "/fuel/curve"));

  auto* fuel_m3 =
      fuel_lph->connect_to(new Linear(0.001f, 0.0f, "/fuel/m3"));

  auto* fuel_sk =
      new SKOutputFloat("propulsion.mainEngine.fuel.rate", "/fuel/sk");

  ConfigItem(fuel_sk)
      ->set_title("Fuel Flow SK Path")
      ->set_sort_order(410);

  fuel_m3->connect_to(fuel_sk);
}

// ============================================================================
// ENGINE HOURS
// ============================================================================
void setup_engine_hours() {

  auto* hours = new EngineHours("/engine/hours");

  ConfigItem(hours)
      ->set_title("Engine Hours")
      ->set_description("Editable initial value; increments when RPM > 500.")
      ->set_sort_order(510);

  rpm_frequency->connect_to(hours);

  auto* hours_sk =
      new SKOutputFloat("propulsion.mainEngine.runTime", "/engine/hours/sk");

  ConfigItem(hours_sk)
      ->set_title("Engine Hours SK Path")
      ->set_sort_order(511);

  hours->connect_to(hours_sk);
}



// ============================================================================
// DEBUG PANEL (SK only, not UI)
// ============================================================================
void setup_debug_sk() {

  auto* dbg_rpm = new DebugValue("RPM Debug", "/debug/rpm");
  rpm_frequency->connect_to(dbg_rpm);

  auto* dbg_clt = new DebugValue("Coolant Debug", "/debug/coolant");
  coolant_temp_c->connect_to(dbg_clt);
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  sensesp_app->get_event_loop()->tick();
}
