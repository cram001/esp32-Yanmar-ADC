// Yanmar marine diesel engine monitoring with SensESP
// Converts analogue engine sensors to SignalK over WiFi (signalK then sends to NMEA2000 network))
// Uses OneWire sensors for exhaust elbow, alternator and engine compartment temps
// Uses voltage divider for coolaant temp sender (NTC thermistor)
// Uses op-amp,octocoupler  schmitt trigger and magnetic pickup for RPM measurement
// Not implemented yet: coolant temp sender

#include <memory>

//common
#include "sensesp/signalk/signalk_output.h"
#include "sensesp_app_builder.h"

//onewire
#include "sensesp/ui/config_item.h"
#include "sensesp_onewire/onewire_temperature.h"

//engine RPM 
#include "sensesp/transforms/frequency.h"
#include "sensesp/sensors/digital_input.h"

// coolant temp sender 
#include "sender_resistance.h"
#include "sensesp/sensors/analog_input.h"


//transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/transforms/integrator.h"
#include "sensesp/transforms/median.h"



//engine hours
#include "engine_hours.h"

// DEBUG PANEL
#include "debug_value.h"


using namespace reactesp;
using namespace sensesp;
using namespace sensesp::onewire;

void setup() {
  SetupLogging();

  // Create the global SensESPApp() object.
  SensESPAppBuilder builder;
  //sensesp_app = builder.get_app();

  sensesp_app = (&builder)
                    // Set a custom hostname for the app.
                    ->set_hostname("esp32-yanmar")
                    ->set_wifi_client("papasmurf2", "2flyornot2fly")
                    // Optionally, hard-code the WiFi and Signal K server
                    // settings. This is normally not needed.
                    //->set_wifi_client("My WiFi SSID", "my_wifi_password")
                    //->set_wifi_access_point("OpenPlotter", "my_signal_k_password")
                    ->enable_ota("bateau1!") // Enable OTA with a password
                    ->set_sk_server("192.168.150.95", 3000)
                    ->get_app();

  /*
     Find all the sensors and their unique addresses. Then, each new instance
     of OneWireTemperature will use one of those addresses. You can't specify
     which address will initially be assigned to a particular sensor, so if you
     have more than one sensor, you may have to swap the addresses around on
     the configuration page for the device. (You get to the configuration page
     by entering the IP address of the device into a browser.)
  */

  /*
     Tell SensESP where the sensor is connected to the board
     ESP32 pins are specified as just the X in GPIOX (pin number on the board itself)
  */
  uint8_t s1_pin = 4;  //engine compartment
  uint8_t s2_pin = 16;  //exhaust elbow
  uint8_t s3_pin = 12;  //12v alternator

// setup for coolant temp sender

// ---------------------------------------------------------
  // 1. ADC Input
  // ---------------------------------------------------------
  const uint8_t kAdcPin = 34;

  auto* adc_input = new sensesp::AnalogInput(
      kAdcPin,          // ADC GPIO
      10.0,             // sample rate Hz
      "/Coolant/adc"    // config path
  );



  // setup for engine RPM
  uint8_t rpm_pin = 25; // GPIO pin where the magnetic pickup is connected
  const float multiplier = 1.0 / 116.0; // 116 teeth on the gear

// Back to OneWire temp stuff...
// 3 seperate sensor busses

  DallasTemperatureSensors* dts1 = new DallasTemperatureSensors(s1_pin);
  
  DallasTemperatureSensors* dts2 = new DallasTemperatureSensors(s2_pin);
  
  DallasTemperatureSensors* dts3 = new DallasTemperatureSensors(s3_pin);

  // Define how often SensESP should read the sensor(s) in milliseconds
  uint read_delay = 500;

  // Below are temperatures sampled and sent to Signal K server
  // To find valid Signal K Paths that fits your need you look at this link:
  // https://signalk.org/specification/1.4.0/doc/vesselsBranch.html

  // Measure engine compartment temperature
  auto compartment_temp =
      new OneWireTemperature(dts1, read_delay, "/coolantTemperature/oneWire");

  ConfigItem(compartment_temp)
      ->set_title("Engine Compartment Temperature")
      ->set_description("Temperature of the engine compartment")
      ->set_sort_order(100);

  auto compartment_temp_calibration =
      new Linear(1.0, 0.0, "/compartmentTemperature/linear");

  ConfigItem(compartment_temp_calibration)
      ->set_title("Engine Compartment Temperature Calibration")
      ->set_description("Calibration for the engine compartment temperature sensor")
      ->set_sort_order(200);

  auto compartment_temp_sk_output = new SKOutputFloat(
      "environment.inside.engineRoom.Temperature", "/compartmentTemperature/skPath");

  ConfigItem(compartment_temp_sk_output)
      ->set_title("Engine Compartment Temperature Signal K Path")
      ->set_description("Signal K path for the engine compartment temperature")
      ->set_sort_order(300);

  compartment_temp->connect_to(compartment_temp_calibration)
      ->connect_to(compartment_temp_sk_output);

  // Measure exhaust elbow temperature
  auto* exhaust_temp =
      new OneWireTemperature(dts2, read_delay, "/exhaustTemperature/oneWire");
  
  ConfigItem(exhaust_temp)
      ->set_title("Exhaust Elbow Temperature")
      ->set_description("Temperature of the exhaust")
      ->set_sort_order(400);

  
      auto* exhaust_temp_calibration =
      new Linear(1.0, 0.0, "/exhaustTemperature/linear");

  ConfigItem(exhaust_temp_calibration)
      ->set_title("Exhaust Elbow Temperature Calibration")
      ->set_description("Calibration for the exhaust temperature sensor")
      ->set_sort_order(500);

   auto* exhaust_temp_sk_output = new SKOutputFloat(
      "propulsion.mainEngine.exhaustTemperature", "/exhaustTemperature/skPath");


  ConfigItem(exhaust_temp_sk_output)
      ->set_title("Exhaust Temperature Signal K Path")
      ->set_description("Signal K path for the exhaust temperature")
      ->set_sort_order(600);
  
  exhaust_temp->connect_to(exhaust_temp_calibration)
      ->connect_to(exhaust_temp_sk_output);


  // Measure temperature of 12v alternator
  auto* alt_12v_temp =
      new OneWireTemperature(dts3, read_delay, "/12vAltTemperature/oneWire");

        ConfigItem(alt_12v_temp)
      ->set_title("12V Alternator Temperature")
      ->set_description("Temperature of the 12V alternator")
      ->set_sort_order(700);

  auto* alt_12v_temp_calibration =
      new Linear(1.0, 0.0, "/12vAltTemperature/linear");

        ConfigItem(alt_12v_temp_calibration)
      ->set_title("12V Alternator Temperature Calibration")
      ->set_description("Calibration for the 12v alternator sensor")
      ->set_sort_order(800);

      auto* alt_12v_temp_sk_output = new SKOutputFloat(
      "electrical.alternators.mainengine.temperature", "/12vAltTemperature/skPath");

  ConfigItem(alt_12v_temp_sk_output)
      ->set_title("12V alternator Temperature Signal K Path")
      ->set_description("Signal K path for the 12v alternator temperature")
      ->set_sort_order(900);

  alt_12v_temp->connect_to(alt_12v_temp_calibration)
      ->connect_to(alt_12v_temp_sk_output);


//// Engine RPM measurement via magnetic pickup on 116 tooth gear
const char* sk_path = "propulsion.mainengine.revolutions";
const char* config_path = "/sensors/engine_rpm";
const char* config_path_calibrate = "/sensors/engine_rpm/calibrate";
const char* config_path_skpath = "/sensors/engine_rpm/sk";

  auto* rpm_sensor = new DigitalInputCounter(rpm_pin, INPUT_PULLUP, RISING, read_delay);

  auto frequency = new Frequency(multiplier, config_path_calibrate);

  ConfigItem(frequency)
      ->set_title("Frequency")
      ->set_description("Frequency of the engine RPM signal")
      ->set_sort_order(1000);

  auto frequency_sk_output = new SKOutput<float>(sk_path, config_path_skpath);

  ConfigItem(frequency_sk_output)
      ->set_title("Frequency SK Output Path")
      ->set_sort_order(1001);

  rpm_sensor
      ->connect_to(frequency)             // connect the output of sensor
                                          // to the input of Frequency()
      ->connect_to(frequency_sk_output);  // connect the output of Frequency()
                                          // to a Signal K Output as a number

// ---------------------------------------------------------
// Engine coolant temp sender
// ---------------------------------------------------------

// Undo voltage divider (100k / 350k = 0.285714)
constexpr float divider_gain = 3.5f;

auto* gauge_voltage =
    adc_input->connect_to(new sensesp::Linear(divider_gain, 0.0));

// ---------------------------------------------------------
// Apply Exponential Smoothing BEFORE resistance & curve
// ---------------------------------------------------------
const float ewma_alpha = 0.10f;   // Recommended for NTC senders

auto* coolant_smoother =
    gauge_voltage->connect_to(new sensesp::Median(5, "/coolant/median"));

// ---------------------------------------------------------
// Convert gauge voltage → sender resistance
// ---------------------------------------------------------
constexpr float supply_voltage = 12.0f;
constexpr float rgauge = 1035.0f;

auto* sender_res =
    coolant_smoother->connect_to(new SenderResistance(supply_voltage, rgauge));

// ---------------------------------------------------------
// Sender resistance → temperature (°C)
// ---------------------------------------------------------
std::set<sensesp::CurveInterpolator::Sample> r_to_temp{
    { -1.0f,  0.0f },   // error state
    {1352.0f, 12.7f},
    {207.0f,  56.0f},
    {131.0f,  63.9f},
    {88.0f,   70.0f},
    {112.0f,  72.0f},
    {104.0f,  76.7f},
    {97.0f,   80.0f},
    {90.9f,  82.5f},
    {85.5f,  85.0f},
    {45.0f, 100.0f},
    {29.6f, 121.0f}
};

auto* temp_c =
    sender_res->connect_to(new sensesp::CurveInterpolator(&r_to_temp));

// ---------------------------------------------------------
// Output coolant temperature to Signal K
// ---------------------------------------------------------
auto* coolant_temp_sk_output =
    new SKOutputFloat("propulsion.mainEngine.coolantTemperature",
                      "/coolantTemperature/skPath");

ConfigItem(coolant_temp_sk_output)
    ->set_title("Coolant Temperature Signal K Path")
    ->set_description("Signal K path for the coolant temperature")
    ->set_sort_order(1100);

temp_c->connect_to(coolant_temp_sk_output);

  // fuel flow sender

  // ---------------------------------------------------------
// ENGINE FUEL FLOW (based on RPM → fuel curve)
// ---------------------------------------------------------

// RPM → fuel consumption curve (L/hr)
std::set<sensesp::CurveInterpolator::Sample> rpm_to_fuel_lph{
    {500.0f,  0.6f},
    {850.0f,  0.7f},
    {1500.0f, 1.8f},
    {2000.0f, 2.3f},
    {2800.0f, 2.7f},
    {3000.0f, 3.0f},
    {3400.0f, 4.5f},
    {3800.0f, 6.0f},
};

// Convert L/hr → m³/hr (multiply by 0.001)
auto* fuel_flow_lph =
    frequency->connect_to(new sensesp::CurveInterpolator(&rpm_to_fuel_lph));

auto* fuel_flow_m3ph =
    fuel_flow_lph->connect_to(new sensesp::Linear(0.001f, 0.0f));

// Output to Signal K
auto* fuel_flow_sk_output = new SKOutputFloat(
    "propulsion.mainEngine.fuel.rate",
    "/fuel/rate/skPath"
);

ConfigItem(fuel_flow_sk_output)
    ->set_title("Fuel Flow SK Path")
    ->set_description("Signal K path for fuel rate")
    ->set_sort_order(1200);

fuel_flow_m3ph->connect_to(fuel_flow_sk_output);


// ---------------------------------------------------------
// ENGINE HOURS
// ---------------------------------------------------------

// Create engine-hours accumulator
auto* engine_hours = new EngineHours("/engine/hours");

// Add to SensESP UI
ConfigItem(engine_hours)
    ->set_title("Engine Hours")
    ->set_description("Initial engine hours (user editable). Value increases whenever RPM > 500.")
    ->set_sort_order(1300);

// Feed RPM into the hour counter
frequency->connect_to(engine_hours);

// Output engine hours to Signal K
auto* engine_hours_sk_output =
    new SKOutputFloat("propulsion.mainEngine.runTime",
                      "/engine/hours/skPath");

engine_hours->connect_to(engine_hours_sk_output);

// ---------------------------------------------------------
// DEBUG PANEL
// ---------------------------------------------------------
#include "debug_value.h"

// Debug corrected ADC voltage (after divider, before smoothing)
auto* dbg_adc = new sensesp::DebugValue("ADC Voltage", "/debug/adc");
ConfigItem(dbg_adc)
    ->set_title("ADC Voltage")
    ->set_description("Raw ADC reading after divider correction")
    ->set_sort_order(2000);
gauge_voltage->connect_to(dbg_adc);

// Debug smoothed voltage
auto* dbg_smooth = new sensesp::DebugValue("Smoothed Voltage", "/debug/coolant_smooth");
ConfigItem(dbg_smooth)
    ->set_title("Coolant Smoothed Voltage")
    ->set_description("After EWMA smoothing, before resistance calc")
    ->set_sort_order(2001);
coolant_smoother->connect_to(dbg_smooth);

// Debug resistance
auto* dbg_res = new sensesp::DebugValue("Sender Resistance", "/debug/resistance");
ConfigItem(dbg_res)
    ->set_title("Sender Resistance")
    ->set_description("Calculated resistance of coolant sender")
    ->set_sort_order(2002);
sender_res->connect_to(dbg_res);

// Debug °C
auto* dbg_temp = new sensesp::DebugValue("Coolant Temp", "/debug/temp");
ConfigItem(dbg_temp)
    ->set_title("Coolant Temperature (debug)")
    ->set_description("Temperature before sending to SK")
    ->set_sort_order(2003);
temp_c->connect_to(dbg_temp);

// Debug raw RPM
auto* dbg_rpm = new sensesp::DebugValue("RPM", "/debug/rpm");
ConfigItem(dbg_rpm)
    ->set_title("RPM (debug)")
    ->set_description("RPM as computed from frequency")
    ->set_sort_order(2003);
frequency->connect_to(dbg_rpm);

// Debug fuel flow (L/hr)
auto* dbg_fuel_lph = new sensesp::DebugValue("Fuel LPH", "/debug/fuel_lph");
ConfigItem(dbg_fuel_lph)
    ->set_title("Fuel Flow (L/hr)")
    ->set_description("Interpolated fuel consumption")
    ->set_sort_order(2004);
fuel_flow_lph->connect_to(dbg_fuel_lph);

// Debug fuel flow (m³/hr)
auto* dbg_fuel_m3ph = new sensesp::DebugValue("Fuel m3/hr", "/debug/fuel_m3hr");
ConfigItem(dbg_fuel_m3ph)
    ->set_title("Fuel Flow (m³/hr)")
    ->set_description("Converted fuel flow")
    ->set_sort_order(2005);
fuel_flow_m3ph->connect_to(dbg_fuel_m3ph);

// Debug engine hours (raw value)
auto* dbg_hrs = new sensesp::DebugValue("Engine Hours", "/debug/hours");
ConfigItem(dbg_hrs)
    ->set_title("Engine Hours (debug)")
    ->set_description("Raw engine hour accumulation before rounding")
    ->set_sort_order(2006);
engine_hours->connect_to(dbg_hrs);


}

// main program loop
void loop() {
  static auto event_loop = sensesp_app->get_event_loop();
  event_loop->tick();
}