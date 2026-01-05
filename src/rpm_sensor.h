#pragma once

/*
 * ============================================================================
 * RPM SENSOR — STABILIZED, ECU-LIKE BEHAVIOR
 * ============================================================================
 *
 * RULES
 * -----
 *  • Raw frequency is a measurement and may glitch
 *  • Published RPM is a STATE
 *  • NEVER emit NaN downstream
 *
 * BEHAVIOR
 * --------
 *  • Engine stopped        → RPM = 0
 *  • Engine running        → RPM stable
 *  • Brief pulse miss      → HOLD last value
 *  • Sustained loss        → RPM = 0
 *
 * CANONICAL OUTPUTS
 * -----------------
 *  • g_engine_rev_s_smooth   (rev/s)  ← AUTHORITATIVE
 *  • g_engine_rad_s_smooth   (rad/s)  ← derived
 *
 * DEBUG
 * -----
 *  • raw frequency
 *  • raw RPM
 *  • stabilized RPM
 * ============================================================================
 */

#include <Arduino.h>
#include <cmath>

#include <sensesp/sensors/digital_input.h>
#include <sensesp/transforms/frequency.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;

// ============================================================================
// EXTERNALS (defined in main.cpp)
// ============================================================================
extern const uint8_t PIN_RPM;
extern const float   RPM_MULTIPLIER;   // pulses/sec → rev/sec

ValueProducer<float>* g_engine_rev_s_smooth = nullptr;
ValueProducer<float>* g_engine_rad_s_smooth = nullptr;

// ============================================================================
// CONSTANTS
// ============================================================================
constexpr float ENGINE_RUNNING_RPM = 500.0f;
constexpr unsigned long RPM_HOLD_MS = 2000;   // tolerate 2 s glitches
constexpr float RAD_PER_REV = 6.283185307f;

// ============================================================================
// SETUP — RPM SENSOR
// ============================================================================
inline void setup_rpm_sensor() {

  // --------------------------------------------------------------------------
  // Digital input from magnetic pickup
  // --------------------------------------------------------------------------
  auto* rpm_input = new DigitalInput(
      PIN_RPM,
      INPUT_PULLUP,
      CHANGE
  );

  // Raw frequency (pulses/sec)
  auto* frequency = rpm_input->connect_to(
      new Frequency(RPM_MULTIPLIER)
  );

#if ENABLE_DEBUG_OUTPUTS
  frequency->connect_to(
      new SKOutputFloat("debug.engine.rpm.frequency_hz")
  );
#endif

  // pulses/sec → RPM (raw, may be NaN)
  auto* rpm_raw = frequency->connect_to(
      new LambdaTransform<float,float>([](float rps){
        if (std::isnan(rps) || rps <= 0.0f) {
          return NAN;
        }
        return rps * 60.0f;
      })
  );

#if ENABLE_DEBUG_OUTPUTS
  rpm_raw->connect_to(
      new SKOutputFloat("debug.engine.rpm.raw")
  );
#endif

  // --------------------------------------------------------------------------
  // STABILIZER — HOLD LAST VALID RPM
  // --------------------------------------------------------------------------
  auto* rpm_stable = rpm_raw->connect_to(
      new LambdaTransform<float,float>([](float rpm){

        static float last_valid_rpm = 0.0f;
        static unsigned long last_valid_ms = 0;

        unsigned long now = millis();

        // Valid RPM?
        if (!std::isnan(rpm) && rpm >= ENGINE_RUNNING_RPM) {
          last_valid_rpm = rpm;
          last_valid_ms = now;
          return rpm;
        }

        // Hold last value during glitch window
        if ((now - last_valid_ms) < RPM_HOLD_MS) {
          return last_valid_rpm;
        }

        // Sustained loss → engine stopped
        last_valid_rpm = 0.0f;
        return 0.0f;
      })
  );

#if ENABLE_DEBUG_OUTPUTS
  rpm_stable->connect_to(
      new SKOutputFloat("debug.engine.rpm.stable")
  );
#endif

  // --------------------------------------------------------------------------
  // RPM → rev/s (CANONICAL)
  // --------------------------------------------------------------------------
  g_engine_rev_s_smooth = rpm_stable->connect_to(
      new LambdaTransform<float,float>([](float rpm){
        return rpm / 60.0f;   // NEVER NaN
      })
  );

#if ENABLE_DEBUG_OUTPUTS
  g_engine_rev_s_smooth->connect_to(
      new SKOutputFloat("debug.engine.revolutions_hz")
  );
#endif

  // --------------------------------------------------------------------------
  // rev/s → rad/s (SignalK / N2K)
  // --------------------------------------------------------------------------
  g_engine_rad_s_smooth = g_engine_rev_s_smooth->connect_to(
      new LambdaTransform<float,float>([](float rev_s){
        return rev_s * RAD_PER_REV;
      })
  );

  // Signal K output (rad/s)
  g_engine_rad_s_smooth->connect_to(
      new SKOutputFloat(
          "propulsion.engine.revolutions",
          "/config/outputs/sk/engine_revolutions"
      )
  );

  // --------------------------------------------------------------------------
  // UI configuration
  // --------------------------------------------------------------------------
  ConfigItem(rpm_input)
      ->set_title("RPM Pickup Input")
      ->set_description("Magnetic pickup for engine RPM");
}
