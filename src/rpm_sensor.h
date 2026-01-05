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
  );#pragma once

// ============================================================================
// RPM SENSOR MODULE (Magnetic Pickup / Flywheel Teeth)
// ============================================================================
//
// CANONICAL ENGINE SPEED CONTRACT
// -------------------------------
// • Canonical engine speed = **revolutions per second (rev/s, Hz)**
// • This signal MUST be smoothed and free of transient 0 / NAN
// • engine_hours and engine_performance MUST consume this signal
//
// Raw Frequency output is NOT suitable for engine logic.
//
// Signal map produced here:
//   g_frequency             → rev/s (raw, diagnostic only)
//   g_engine_rev_s_smooth   → rev/s (SMOOTHED, CANONICAL)
//   g_engine_rad_s          → rad/s (DERIVED, INTERNAL ONLY)
//
// NOTE:
// Signal K propulsion.engine.revolutions MUST be published in Hz (rev/s).
// Conversion to rad/s is handled downstream (SK → NMEA2000 PGN 127488).
// ============================================================================

#include <cmath>
#include <deque>

#include <sensesp/sensors/digital_input.h>
#include <sensesp/transforms/frequency.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;

// -----------------------------------------------------------------------------
// Externals provided by main.cpp
// -----------------------------------------------------------------------------
extern const uint8_t PIN_RPM;
extern const float   RPM_TEETH;

// Shared signals (defined in main.cpp)
extern Frequency*            g_frequency;           // rev/s (raw)
extern ValueProducer<float>* g_engine_rev_s_smooth; // rev/s (CANONICAL)
extern ValueProducer<float>* g_engine_rad_s;        // rad/s (derived)

// -----------------------------------------------------------------------------
// RPM smoothing parameters
// -----------------------------------------------------------------------------
static constexpr uint32_t RPM_AVG_WINDOW_MS = 1000;
static constexpr uint32_t RPM_STALL_TIMEOUT_MS = 4000;  // allow short gaps before dropping to NAN

struct RpmSample {
  float    rps;
  uint32_t t_ms;
};

// -----------------------------------------------------------------------------
// RPM sensor setup
// -----------------------------------------------------------------------------
inline void setup_rpm_sensor() {

  // ---------------------------------------------------------------------------
  // 1. Pulse counter
  // ---------------------------------------------------------------------------
  auto* pulse_input = new DigitalInputCounter(
      PIN_RPM,
      INPUT,
      RISING,
      0
  );

  // ---------------------------------------------------------------------------
  // 2. Pulses/sec → rev/s (RAW, UNSAFE)
  // ---------------------------------------------------------------------------
  g_frequency = pulse_input->connect_to(
      new Frequency(
          1.0f / RPM_TEETH,
          "/config/sensors/rpm/frequency"
      )
  );

  // ---------------------------------------------------------------------------
  // 3. Sliding-window smoothing on rev/s (CANONICAL ENGINE SPEED)
  // ---------------------------------------------------------------------------
  static std::deque<RpmSample> rpm_buf;

  g_engine_rev_s_smooth = g_frequency->connect_to(
      new LambdaTransform<float,float>(
          [](float rps) {
            static uint32_t last_sample_ms = 0;
            const uint32_t now = millis();

            if (!std::isnan(rps) && rps > 0.1f) {
              rpm_buf.push_back({rps, now});
              last_sample_ms = now;
            }

            while (!rpm_buf.empty() &&
                   (now - rpm_buf.front().t_ms) > RPM_AVG_WINDOW_MS) {
              rpm_buf.pop_front();
            }

            // If we've gone longer than the smoothing window without any new
            // valid pulses, drop to NAN (after a grace period) so downstream
            // logic can fault/idle without flapping.
            if (!rpm_buf.empty() && last_sample_ms != 0 &&
                (now - last_sample_ms) > RPM_STALL_TIMEOUT_MS) {
              rpm_buf.clear();
            }

            if (rpm_buf.empty()) {
              return NAN;
            }

            float sum = 0.0f;
            for (const auto& s : rpm_buf) {
              sum += s.rps;
            }

            return sum / rpm_buf.size();
          },
          "/config/sensors/rpm/rev_per_sec_smooth"
      )
  );

  // ---------------------------------------------------------------------------
  // 4. rev/s → rad/s (DERIVED, INTERNAL ONLY)
  // ---------------------------------------------------------------------------
  constexpr float PI_F = 3.14159265f;

  g_engine_rad_s = g_engine_rev_s_smooth->connect_to(
      new LambdaTransform<float,float>(
          [](float rps) {
            return std::isnan(rps) ? NAN : (rps * 2.0f * PI_F);
          },
          "/config/sensors/rpm/rad_per_sec_smooth"
      )
  );

#if ENABLE_DEBUG_OUTPUTS
  // ---------------------------------------------------------------------------
  // Debug outputs (explicit units)
  // ---------------------------------------------------------------------------
  g_frequency->connect_to(
      new SKOutputFloat("debug.engine.revolutions_hz_raw")
  );

  g_engine_rev_s_smooth->connect_to(
      new SKOutputFloat("debug.engine.revolutions_hz")
  );

  g_engine_rad_s->connect_to(
      new SKOutputFloat("debug.engine.revolutions_rad_s")
  );

  g_engine_rev_s_smooth->connect_to(
      new LambdaTransform<float,float>([](float rps){
        return std::isnan(rps) ? NAN : (rps * 60.0f);
      })
  )->connect_to(
      new SKOutputFloat("debug.engine.rpm")
  );
#endif

  // ---------------------------------------------------------------------------
  // 5. Signal K output (Hz / rev/s — NOT rad/s)
  // ---------------------------------------------------------------------------
  auto* sk_revs = new SKOutputFloat(
      "propulsion.engine.revolutions",
      "/config/outputs/sk/revolutions"
  );

  // Signal K expects Hz (rev/s)
  g_engine_rev_s_smooth->connect_to(sk_revs);

  ConfigItem(sk_revs)
      ->set_title("Engine Revolutions (Hz)")
      ->set_description(
          "Smoothed propulsion.engine.revolutions in Hz (rev/s). "
          "Converted to rad/s downstream for NMEA 2000 PGN 127488."
      );
}

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
