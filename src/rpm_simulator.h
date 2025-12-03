#pragma once
#include <Arduino.h>

#ifdef RPM_SIMULATOR

// ------------------------------------------------------
// Pins
// ------------------------------------------------------
static const int RPM_SIM_PIN = 26;   // Simulator OUTPUT D3
static const int RPM_INPUT_PIN = 25; // Your real RPM input pin D2

// ------------------------------------------------------
// Simulator parameters
// ------------------------------------------------------
static const int TEETH = 116;

static const int MIN_RPM       = 1000;
static const int MAX_RPM       = 2500;
static const int SWEEP_TIME_MS = 10000; // 10s sweep

volatile uint32_t pulse_count = 0;

hw_timer_t* sim_timer = nullptr;
volatile float current_rpm = MIN_RPM;


// ------------------------------------------------------
// High-speed ISR: toggle simulator output pin
// ------------------------------------------------------
void IRAM_ATTR simulatePulse() {
    pulse_count++;
    digitalWrite(RPM_SIM_PIN, !digitalRead(RPM_SIM_PIN));
}


// ------------------------------------------------------
// Calculate new RPM & timer interval
// ------------------------------------------------------
void updateRPM() {
    static uint32_t start = millis();
    uint32_t now = millis();
    uint32_t elapsed = now - start;

    if (elapsed > SWEEP_TIME_MS) {
        start = millis();
        elapsed = 0;
    }

    float ratio = (float)elapsed / (float)SWEEP_TIME_MS;
    current_rpm = MIN_RPM + ratio * (MAX_RPM - MIN_RPM);

    // Convert RPM → microseconds per pulse
    float pulses_per_sec = (current_rpm * TEETH) / 60.0f;
    float us_per_pulse   = 1e6f / pulses_per_sec;

    timerAlarmWrite(sim_timer, (uint32_t)us_per_pulse, true);
}


// ------------------------------------------------------
// Initialization
// ------------------------------------------------------
void setupRPMSimulator() {
    // OUTPUT for the simulated waveform
    pinMode(RPM_SIM_PIN, OUTPUT);
    digitalWrite(RPM_SIM_PIN, LOW);

    // Ensure the input pin is ready
    pinMode(RPM_INPUT_PIN, INPUT);  // DigitalInputCounter sets proper mode later

    // Use hardware timer #2
    sim_timer = timerBegin(2, 80, true);
    timerAttachInterrupt(sim_timer, &simulatePulse, true);

    // Start timer at MIN_RPM
    float pulses_per_sec = (MIN_RPM * TEETH) / 60.0f;
    float us_per_pulse   = 1e6f / pulses_per_sec;
    timerAlarmWrite(sim_timer, (uint32_t)us_per_pulse, true);
    timerAlarmEnable(sim_timer);

    Serial.println("=== RPM SIMULATOR ACTIVE ===");
    Serial.println("Simulator pin: GPIO27  →  RPM input pin: GPIO25");
}


// ------------------------------------------------------
// Loop helper
// ------------------------------------------------------
void loopRPMSimulator() {
    updateRPM();
}

#endif  // RPM_SIMULATOR
