// ============================================================================
// Yanmar Diesel Engine Monitoring - SensESP v3.1.1
// ============================================================================
// Features:
//  - Engine Coolant sender via ADC (FireBeetle calibrated) (works in conjuction with gauge)
//  - 3x OneWire temperature sensors
//  - RPM via magnetic pickup
//  - Fuel flow estimation based on RPM
//  - Engine hours accumulator
//  - SK debug output
//  - OTA update
//  - Full UI configuration for SK paths and calibration, setting wifi and SK server address
// values sent to SignalK IAW https://signalk.org/specification/1.5.0/doc/vesselsBranch.html
//  Note: you need a signalk server and ideally a wifi router (although CerboGX can act
//  as AP).
//  In signalk, configure SK to N2K add-in to forward values to NMEA2000 network if desired.)
// oil pressure sender sierra OP24301 Range-1 Gauge: 240 Ohms at 0 PSI
//Range-1 Gauge: 200 Ohms at 10 PSI
//Range-1 Gauge: 160 Ohms at 20 PSI
//Range-1 Gauge: 140 Ohms at 30 PSI
//Range-1 Gauge: 120 Ohms at 40 PSI
//Range-1 Gauge: 100 Ohms at 50 PSI
//Range-1 Gauge: 90 Ohms at 60 PSI
//Range-1 Gauge: 75 Ohms at 70 PSI
//Range-1 Gauge: 60 Ohms at 80 PSI
//Range-1 Gauge: 45 Ohms at 90 PSI
//Range-1 Gauge: 33 Ohms at 100 PSI
// ============================================================================
// ============================================================================
#ifndef UNIT_TEST

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
#include "driver/adc.h"

// Transforms
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/median.h"
#include "sensesp/transforms/frequency.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/transforms/moving_average.h"


// Custom functions
#include "sender_resistance.h"
#include "engine_hours.h"
#include "engine_load.h"
#include "calibrated_analog_input.h"
#include "oil_pressure_sender.h"


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
void setup_temperature_sensors();
void setup_coolant_sender();
void setup_rpm_sensor();
void setup_fuel_flow();
void setup_engine_hours();

// -----------------------------------------------
// PIN DEFINITIONS — FIREBEETLE ESP32-E   EDIT THESE IF REQUIRE FOR YOUR BOARD
// -----------------------------------------------
static const uint8_t PIN_TEMP_COMPARTMENT = 4;  // one wire for engine compartment /digital  12
static const uint8_t PIN_TEMP_EXHAUST     = 16;  // one wire,strapped to exhaust elbow digital 11
static const uint8_t PIN_TEMP_ALT_12V     = 17; // extra sensor... could be aft cabin?? / digital 10 NOT USED

static const uint8_t PIN_ADC_COOLANT      = 39;  // engine's coolant temperature sender
static const uint8_t PIN_RPM              = 25;   // magnetic pickup for RPM (digital input) digital 2

static const uint8_t PIN_ADC_OIL_PRESSURE = 36;   // choose free ADC pin
static const float OIL_ADC_REF_VOLTAGE = 2.5f;    // ref voltage of the Firebeetle esp32-e
static const float OIL_PULLUP_RESISTOR = 220.0f;   // Resistance (ohm) in mid-range of oil px sender values


// -----------------------------------------------
// CONSTANTS
// -----------------------------------------------
static const float ADC_SAMPLE_RATE_HZ = 10.0f;

// FOR Yanmar COOLANT TEMP SENDER:
// DFROBOT Gravity Voltage Divider (SEN0003 / DFR0051)
// R1 = 30kΩ, R2 = 7.5kΩ → divider ratio = 1/5 → gain = 5.0
// 
// ⚠️ VOLTAGE ANALYSIS (14.0V max system):
// - Normal operation (29-1352Ω sender): 0.30V - 1.49V ✅ SAFE
// - Open circuit fault (14.0V): 2.8V → EXCEEDS recommended 2.45V spec
// - ESP32 absolute max: 3.6V (won't damage, but out of spec)
// - RISK: Reduced accuracy/lifespan when >2.5V, vulnerable to voltage spikes
// - RECOMMENDED: Add 2.4V Zener diode for hardware protection (~$0.50)
// - Software protection: sender_resistance.h detects open circuit and emits NAN
//
static const float DIV_R1 = 30000.0f;     // Top resistor
static const float DIV_R2 = 7500.0f;      // Bottom resistor
static const float COOLANT_DIVIDER_GAIN = (DIV_R1 + DIV_R2) / DIV_R2;  
// COOLANT_DIVIDER_GAIN = 5.0

static const float COOLANT_SUPPLY_VOLTAGE = 13.5f;  //13.5 volt nominal, indication will be close enough at 12-14 VDC
static const float COOLANT_GAUGE_RESISTOR = 1180.0f;   // Derived from 6.8V @ 1352Ω coolant temp gauge resistance
// recheck this at operating temperature


static const float RPM_TEETH = 116.0f;
static const float RPM_MULTIPLIER = 1.0f / RPM_TEETH;

static const uint32_t ONEWIRE_READ_DELAY_MS = 500;

Frequency* g_frequency = nullptr;
Transform<float, float>* g_fuel_lph = nullptr;  // FIXED



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
      ->set_sk_server("192.168.150.95", 3000);

  sensesp_app = builder.get_app();

new OilPressureSender(
    PIN_ADC_OIL_PRESSURE,
    OIL_ADC_REF_VOLTAGE,
    OIL_PULLUP_RESISTOR,
    ADC_SAMPLE_RATE_HZ,
    "propulsion.mainEngine.oilPressure",
    "/config/sensors/oil_pressure"
);

  setup_temperature_sensors();
  setup_coolant_sender();
  setup_rpm_sensor();
  setup_fuel_flow();
  setup_engine_hours();
  setup_engine_load(g_frequency, g_fuel_lph);


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
// COOLANT SENDER — FireBeetle ESP32-E (SensESP v3.1.1 compatible)
// ============================================================================
// Features:
//  * Correct ADC attenuation (DB_11 for 2.5V max range)
//  * Voltage divider compensation ×5 (DFRobot SEN003)
//  * ADC calibration factor applied once (board-specific)
//  * Safe median filtering
//  * Safe sender resistance calculation
//  * Temperature lookup via American sender curve
//  * Full debug chain
// ============================================================================

void setup_coolant_sender() {

  //
  // STEP 1 — Calibrated ADC input (FireBeetle ESP32-E, 2.5 V reference)
  //
  auto* adc_raw = new CalibratedAnalogInput(
      PIN_ADC_COOLANT,
      ADC_SAMPLE_RATE_HZ,
      "/config/sensors/coolant/adc_raw"
  );

  //
  // STEP 2 — Direct volts (Linear(1.0f) preserved for UI fine tuning)
  //
  auto* volts_raw = adc_raw->connect_to(
      new Linear(
          1.0f,
          0.0f,
          "/config/sensors/coolant/volts_raw"
      )
  );

  //
  // STEP 3 — Undo DFRobot 30k / 7.5k divider (exact 5.0x multiplier)
  //
  auto* sender_voltage = volts_raw->connect_to(
      new Linear(
          COOLANT_DIVIDER_GAIN,     // = 5.0
          0.0f,
          "/config/sensors/coolant/sender_voltage"
      )
  );

  //
  // STEP 4 — Median filtering to reject ADC spike noise
  //
  auto* sender_voltage_smooth = sender_voltage->connect_to(
      new Median(
          5,
          "/config/sensors/coolant/sender_voltage_smooth"
      )
  );

  //
  // STEP 5 — Convert voltage → sender resistance
  //
  auto* sender_res_raw = sender_voltage_smooth->connect_to(
      new SenderResistance(
          COOLANT_SUPPLY_VOLTAGE,    // set nominal 13.5 V
          COOLANT_GAUGE_RESISTOR     // ≈ 1035 Ω
      )
  );

  //
  // STEP 6 — User scaling (fine tuning)
  //
  auto* sender_res_scaled = sender_res_raw->connect_to(
      new Linear(
          1.0f,
          0.0f,
          "/config/sensors/coolant/resistance_scale"
      )
  );

  ConfigItem(sender_res_scaled)
      ->set_title("Engine Coolant Sender Curve Linearity")
      ->set_description("Scale factor applied to sender resistance")
      ->set_sort_order(350);

  //
  // STEP 7 — Convert sender resistance → coolant temp (°C)
  //
  std::set<CurveInterpolator::Sample> ohms_to_temp = {
      {29.6f, 121.0f},
      {45.0f, 100.0f},
      {85.5f,  85.0f},
      {90.9f,  82.5f},
      {97.0f,  80.0f},
      {104.0f, 76.7f},
      {112.0f, 72.0f},
      {131.0f, 63.9f},
      {207.0f, 56.0f},
      {1352.0f,12.7f}
  };

  auto* temp_C = sender_res_scaled->connect_to(
      new CurveInterpolator(
          &ohms_to_temp,
          "/config/sensors/coolant/temp_C"
      )
  );

  //
  // STEP 8 — 5 second moving average (50 samples @ 10 Hz)
  //
auto* temp_C_avg = temp_C->connect_to(
    new MovingAverage(50)   // 5 seconds @ 10Hz
);

  //
  // STEP 9 — Prepare SK output (manually updated every 10 sec)
  //
  auto* sk_coolant = new SKOutputFloat(
      "propulsion.mainEngine.coolantTemperature",
      "/config/outputs/sk/coolant_temp"
  );

  //
  // STEP 10 — Throttle SK output: update every 10 seconds
  //
  sensesp_app->get_event_loop()->onRepeat(
      10000,   // milliseconds
      [temp_C_avg, sk_coolant]() {
        
        float c = temp_C_avg->get();
        if (!isnan(c)) {
          sk_coolant->set_input(c + 273.15f);   // convert °C → K
        }
      }
  );

  //
  // STEP 11 — DEBUG OUTPUTS (conditional compilation)
  //
#if ENABLE_DEBUG_OUTPUTS
  adc_raw->connect_to(new SKOutputFloat("debug.coolant.adc_volts"));
  volts_raw->connect_to(new SKOutputFloat("debug.coolant.volts_raw"));
  sender_voltage->connect_to(new SKOutputFloat("debug.coolant.senderVoltage"));
  sender_voltage_smooth->connect_to(new SKOutputFloat("debug.coolant.senderVoltage_smooth"));
  sender_res_raw->connect_to(new SKOutputFloat("debug.coolant.senderResistance"));
  sender_res_scaled->connect_to(new SKOutputFloat("debug.coolant.senderResistance_scaled"));
  temp_C->connect_to(new SKOutputFloat("debug.coolant.temperatureC_raw"));
  temp_C_avg->connect_to(new SKOutputFloat("debug.coolant.temperatureC_avg"));
#endif
}


// ============================================================================
// RPM SENSOR — UI + EDITABLE SK PATH
// ============================================================================

void setup_rpm_sensor() {

  //
  // 1. Pulse counter (interrupt-driven)
  //
auto* pulse_input = new DigitalInputCounter(
      PIN_RPM,
      INPUT,
      CHANGE,
      0      // reset interval
);

  //
  // 2. Frequency transform: pulses/sec → revolutions/sec
  //
  // multiplier = 1 / number_of_teeth
  //
  g_frequency = pulse_input->connect_to(
      new Frequency(
          1.0f / RPM_TEETH,
          "/config/sensors/rpm/frequency"
      )
  );

  //
  // 3. Convert revolutions/sec → RPM
  //
  auto* rpm_output = g_frequency->connect_to(
      new LambdaTransform<float, float>(
          [](float rev_per_sec) {
            return rev_per_sec * 60.0f;  // Hz → RPM
          },
          "/config/sensors/rpm/rpm_value"
      )
  );

  //
  // 4. Publish RPM to Signal K
  //
  auto* sk_rpm = new SKOutputFloat(
      "propulsion.mainEngine.revolutions",
      "/config/outputs/sk/rpm"
  );

  rpm_output->connect_to(sk_rpm);

  //
  // 5. Debug Outputs (conditional compilation)
  //
#if ENABLE_DEBUG_OUTPUTS
  rpm_output->connect_to(
      new SKOutputFloat("debug.rpm_readable")
  );

  g_frequency->connect_to(
      new SKOutputFloat("debug.rpm_hz")
  );
#endif

  //
  // 6. Config UI
  //
  ConfigItem(sk_rpm)
      ->set_title("RPM SK Path")
      ->set_description("Signal K path for engine RPM")
      ->set_sort_order(500);
}


// ============================================================================
// FUEL FLOW — UI + EDITABLE SK PATH  engine RPM to LPH curve
// ============================================================================


#include "sensesp/transforms/lambda_transform.h"   // ← REQUIRED

void setup_fuel_flow() {

  // -------------------------
  // 1) Convert frequency (Hz) → RPM
  // -------------------------
  auto* rpm_input = g_frequency->connect_to(
      new LambdaTransform<float, float>([](float hz) {
        return hz * 60.0f;  // rev/s → RPM
      })
  );

  // Debug readable RPM (conditional compilation)
#if ENABLE_DEBUG_OUTPUTS
  rpm_input->connect_to(new SKOutputFloat(
      "debug.rpm_readable",
      "/config/outputs/sk/debug_rpm_readable"
  ));
#endif

  // -------------------------
  // 2) RPM → Liters per hour curve
  // -------------------------
  static std::set<CurveInterpolator::Sample> rpm_to_lph = {
      {500, 0.6},
      {850, 0.7},
      {1500, 1.8},
      {2000, 2.3},
      {2800, 2.7},
      {3200, 3.4},
      {3900, 6.5},
  };

  auto* fuel_lph = rpm_input->connect_to(
      new CurveInterpolator(&rpm_to_lph,
                            "/config/sensors/fuel/lph_curve")
  );

  
#if ENABLE_DEBUG_OUTPUTS
  fuel_lph->connect_to(new SKOutputFloat(
      "debug.fuel.lph",
      "/config/outputs/sk/debug_fuel_lph"
  ));
#endif

  g_fuel_lph = fuel_lph;   // expose to engine_load module


  // -------------------------
  // 3) Convert LPH → m³/s  
  //    Only send if RPM ≥ 600
  // -------------------------
  auto* fuel_m3s = rpm_input->connect_to(
      new LambdaTransform<float, float>([fuel_lph](float rpm) {
        if (rpm < 600) {
          return NAN;     // Signal K will ignore it instead of showing null
        }

        float lph = fuel_lph->get();
        if (!isnan(lph)) {
          return (lph / 1000.0f) / 3600.0f;  // L → m³, hr → sec
        }

        return NAN;
      })
  );

  // -------------------------
  // 4) Output final fuel rate in m³/s → Signal K
  // -------------------------
  fuel_m3s->connect_to(new SKOutputFloat(
      "propulsion.mainEngine.fuel.rate",
      "/config/outputs/sk/fuel_rate_m3s"
  ));

  // Debug m³/s output (conditional compilation)
#if ENABLE_DEBUG_OUTPUTS
  fuel_m3s->connect_to(new SKOutputFloat(
      "debug.fuel.m3s",
      "/config/outputs/sk/debug_fuel_m3s"
  ));
#endif

}

// ============================================================================
// ENGINE LOAD
// ============================================================================




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
      "propulsion.mainEngine.runTime",
      "/config/outputs/sk/engine_hours"
  );

  // Connect RPM signal → hours accumulator
  g_frequency->connect_to(hours);

  // Output to SK in seconds
  hours_to_seconds->connect_to(sk_hours);

  // Debug (still outputs hours) - conditional compilation
#if ENABLE_DEBUG_OUTPUTS
  hours->connect_to(new SKOutputFloat("debug.engineHours"));
#endif

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

