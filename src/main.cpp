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
#include "sensesp/transforms/linear.h"
#include "sensesp/ui/config_item.h"
#include "sensesp_onewire/onewire_temperature.h"

//engine RPM 
#include "sensesp/transforms/frequency.h"
#include "sensesp/sensors/digital_input.h"


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
     ESP32 pins are specified as just the X in GPIOX
  */
  uint8_t s1_pin = 4;
  uint8_t s2_pin = 12;
  uint8_t s3_pin = 16;

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

  // Measure coolant temperature
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
      "environment.mainEngine.airTemperature", "/compartmentTemperature/skPath");

  ConfigItem(compartment_temp_sk_output)
      ->set_title("Engine Compartment Temperature Signal K Path")
      ->set_description("Signal K path for the engine compartment temperature")
      ->set_sort_order(300);

  compartment_temp->connect_to(compartment_temp_calibration)
      ->connect_to(compartment_temp_sk_output);

  // Measure exhaust temrature
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

   //NOte: both alternator temps are on same bus (dts3)
  // Measure temperature of 24v alternator
  auto* alt_24v_temp =
      new OneWireTemperature(dts3, read_delay, "/24vAltTemperature/oneWire");
      auto* alt_24v_temp_calibration =
      new Linear(1.0, 0.0, "/24vAltTemperature/linear");
  auto* alt_24v_temp_sk_output = new SKOutputFloat(
      "electrical.alternators.24V.temperature", "/24vAltTemperature/skPath");

  alt_24v_temp->connect_to(alt_24v_temp_calibration)
      ->connect_to(alt_24v_temp_sk_output);

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
      "electrical.alternators.12V.temperature", "/12vAltTemperature/skPath");

  ConfigItem(alt_12v_temp_sk_output)
      ->set_title("12V alternator Temperature Signal K Path")
      ->set_description("Signal K path for the 12v alternator temperature")
      ->set_sort_order(900);

  alt_12v_temp->connect_to(alt_12v_temp_calibration)
      ->connect_to(alt_12v_temp_sk_output);


//// Engine RPM measurement via magnetic pickup on 116 tooth gear
const char* sk_path = "propulsion.main.revolutions";
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


}

// main program loop
void loop() {
  static auto event_loop = sensesp_app->get_event_loop();
  event_loop->tick();
}