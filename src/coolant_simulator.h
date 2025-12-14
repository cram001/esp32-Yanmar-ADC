#pragma once
#include <Arduino.h>

// Enable with -DCOOLANT_SIMULATOR=1
#if COOLANT_SIMULATOR

// ---------------------------
// Pin (DAC2 on classic ESP32)
// ---------------------------
static const int COOLANT_SIM_PIN = 26;   // DAC2 output
static const int COOLANT_ADC_PIN = 39;   // Your existing coolant ADC pin (for reference)

// ---------------------------
// Sweep parameters
// ---------------------------
static const float TEMP_MIN_C = 12.0f;
static const float TEMP_MAX_C = 95.0f;
static const uint32_t COOLANT_SWEEP_TIME_MS = 30000; // 30s sweep

static float g_sim_temp_c = TEMP_MIN_C;

// ---------------------------
// These should match main.cpp constants.
// If you change them in main.cpp, update here too.
// ---------------------------
static const float SIM_COOLANT_SUPPLY_VOLTAGE = 13.5f;
static const float SIM_COOLANT_GAUGE_RESISTOR = 1180.0f;
static const float SIM_DIVIDER_GAIN = 5.0f;   // (R1+R2)/R2

// ---------------------------
// Curve: temp (°C) -> ohms (sender)
// (This is the inverse of your ohms->temp samples.)
// ---------------------------
struct Sample { float tC; float ohms; };

static const Sample kTempToOhms[] = {
  {12.7f, 1352.0f},
  {56.0f,  207.0f},
  {63.9f,  131.0f},
  {72.0f,  112.0f},
  {76.7f,  104.0f},
  {80.0f,   97.0f},
  {82.5f,   90.9f},
  {85.0f,   85.5f},
  {100.0f,  45.0f},
  {121.0f,  29.6f},
};

static inline float lerp(float a, float b, float u) { return a + (b - a) * u; }

// Linear interpolation in the piecewise temp->ohms table
static float ohms_for_temp(float tC) {
  // clamp
  if (tC <= kTempToOhms[0].tC) return kTempToOhms[0].ohms;
  const int n = sizeof(kTempToOhms) / sizeof(kTempToOhms[0]);
  if (tC >= kTempToOhms[n - 1].tC) return kTempToOhms[n - 1].ohms;

  for (int i = 0; i < n - 1; i++) {
    float t0 = kTempToOhms[i].tC;
    float t1 = kTempToOhms[i + 1].tC;
    if (tC >= t0 && tC <= t1) {
      float u = (tC - t0) / (t1 - t0);
      return lerp(kTempToOhms[i].ohms, kTempToOhms[i + 1].ohms, u);
    }
  }
  return kTempToOhms[n - 1].ohms; // fallback
}

// Compute the ADC-pin voltage that your real divider would produce
static float adc_volts_for_temp(float tC) {
  float rs = ohms_for_temp(tC);

  // Series divider: Vsupply -> Rgauge -> node -> Rs -> GND
  float v_sender = SIM_COOLANT_SUPPLY_VOLTAGE * (rs / (SIM_COOLANT_GAUGE_RESISTOR + rs));

  // Your DFRobot divider scales sender node down by 5:1 before ADC
  float v_adc = v_sender / SIM_DIVIDER_GAIN;

  // Keep within DAC range
  if (v_adc < 0.0f) v_adc = 0.0f;
  if (v_adc > 3.3f) v_adc = 3.3f;

  return v_adc;
}

// Convert volts to DAC code (approx. 0..255 -> 0..3.3V)
static uint8_t dac_code_for_volts(float v) {
  float code = (v / 3.3f) * 255.0f;
  if (code < 0.0f) code = 0.0f;
  if (code > 255.0f) code = 255.0f;
  return (uint8_t)(code + 0.5f);
}

static void update_coolant_sim() {
  static uint32_t start_ms = 0;
  uint32_t now = millis();
  if (start_ms == 0) start_ms = now;

  uint32_t elapsed = now - start_ms;
  if (elapsed > COOLANT_SWEEP_TIME_MS) {
    start_ms = now;
    elapsed = 0;
  }

  float ratio = (float)elapsed / (float)COOLANT_SWEEP_TIME_MS;
  g_sim_temp_c = TEMP_MIN_C + ratio * (TEMP_MAX_C - TEMP_MIN_C);

  float v_adc = adc_volts_for_temp(g_sim_temp_c);
  uint8_t code = dac_code_for_volts(v_adc);

  // DAC2 output on GPIO26
  dacWrite(COOLANT_SIM_PIN, code);
}

static void setupCoolantSimulator() {
  // No pinMode needed for DAC, but harmless:
  pinMode(COOLANT_SIM_PIN, OUTPUT);

  // Start at TEMP_MIN_C
  float v_adc = adc_volts_for_temp(TEMP_MIN_C);
  dacWrite(COOLANT_SIM_PIN, dac_code_for_volts(v_adc));

  Serial.println("=== COOLANT SIMULATOR ACTIVE ===");
  Serial.println("GPIO26 (DAC2) -> GPIO39 (ADC coolant) + common GND");
}

static void loopCoolantSimulator() {
  // Update fast enough to look smooth vs your 10 Hz ADC sampling
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 50) { // 20 Hz update
    last = now;
    update_coolant_sim();
  }
}

#endif  // COOLANT_SIMULATOR
