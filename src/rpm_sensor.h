#pragma once

// ============================================================================
// RPM SENSOR MODULE (Magnetic Pickup / Flywheel Teeth)
//
// Purpose
// -------
// Measures engine RPM from a magnetic pickup and publishes it in a form that
// is compatible with both Signal K semantics and strict NMEA 2000 consumers
// (notably Raymarine i70s).
//
// Key design goals
// ----------------
// • Accurate RPM measurement across idle → cruise
// • Stable display behavior (no jitter, no stepping)
// • Compliance with Signal K units (rad/s)
// • Compatibility with NMEA 2000 PGN 127488 (Engine Parameters, Rapid Update)
//
// Architecture overview
// ---------------------
// 1. Hardware pulses are counted via DigitalInputCounter.
// 2. Pulses/sec are converted to revolutions/sec (Hz) using RPM_TEETH.
// 3. rev/s are converted to rad/s (canonical Signal K unit).
// 4. A time-based sliding window average is applied (default: 1 second).
// 5. Smoothed rad/s is published to Signal K.
// 6. Downstream code is responsible for emitting PGN 127488 at 4 Hz.
//
// Important design decisions
// --------------------------
// • Internal measurement rate is decoupled from output/transmit rate.
//   The RPM signal may update irregularly; the NMEA 2000 sender transmits
//   at a fixed cadence (e.g. 250 ms).
//
// • Smoothing uses a *time window*, not a fixed sample count.
//   This avoids aliasing, phase lag, and stepped output when transmitting
//   faster than the measurement rate.
//
// • Raw (unsmoothed) RPM is preserved internally for diagnostics and
//   future calculations (engine load, fuel estimation, etc.).
//
// • NAN values are propagated when RPM is invalid or missing.
//   Downstream PGN emission logic MUST suppress transmission in this case.
//   (Do not transmit NAN or 0 RPM in PGN 127488.)
//
// Raymarine-specific notes
// ------------------------
// • Raymarine i70s will ignore PGN 127488 unless:
//     - Engine speed is provided in rad/s
//     - Update rate is ~4–10 Hz
//     - Engine Instance is 0 or 1
//     - Values are non-null and sane
//
// This module intentionally does NOT control transmit rate or instances.
// That responsibility belongs to the NMEA 2000 output layer.
//
// Tuning
// ------
// • RPM_AVG_WINDOW_MS controls smoothing behavior.
//     - 250 ms  → very responsive, slightly jittery at idle
//     - 1000 ms → smooth, analog-gauge-like (default)
//     - >1500 ms not recommended (perceptible lag)
//
// ---------------------------------------------------------------------------
// Future maintainers:
//   If RPM appears on MFDs but not on an i70s, check PGN 127488 transmit rate
//   and instance handling before changing anything in this file.
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
extern const uint8_t PIN_RPM;     // GPIO connected to magnetic pickup / opto
extern const float RPM_TEETH;     // Flywheel teeth count (pulses per revolution)

// Shared signals (used elsewhere)
extern Frequency* g_frequency;                 // rev/s (Hz), raw
extern ValueProducer<float>* g_engine_rad_s;   // rad/s, raw (unsmoothed)

// -----------------------------------------------------------------------------
// RPM smoothing parameters
// -----------------------------------------------------------------------------
static constexpr uint32_t RPM_AVG_WINDOW_MS = 1000;  // 1 second sliding window

struct RpmSample {
  float    rad_s;   // instantaneous rad/s
  uint32_t t_ms;    // timestamp (millis)
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
  // 2. Pulses/sec → revolutions/sec (Hz)
  // ---------------------------------------------------------------------------
  g_frequency = pulse_input->connect_to(
      new Frequency(
          1.0f / RPM_TEETH,
          "/config/sensors/rpm/frequency"
      )
  );

  // ---------------------------------------------------------------------------
  // 3. rev/s → rad/s (raw, unsmoothed)
  // ---------------------------------------------------------------------------
  g_engine_rad_s = g_frequency->connect_to(
      new LambdaTransform<float, float>(
          [](float rev_s) {
            return std::isnan(rev_s) ? NAN : (rev_s * 2.0f * M_PI);
          },
          "/config/sensors/rpm/rad_per_sec_raw"
      )
  );

  // ---------------------------------------------------------------------------
  // 4. Sliding-window smoothing (time-based)
  // ---------------------------------------------------------------------------
  static std::deque<RpmSample> rpm_buf;

  auto* g_engine_rad_s_smooth = g_engine_rad_s->connect_to(
      new LambdaTransform<float, float>(
          [](float rad_s) {
            const uint32_t now = millis();

            // Accept only sane, positive samples
            if (!std::isnan(rad_s) && rad_s > 0.1f) {
              rpm_buf.push_back({rad_s, now});
            }

            // Drop samples outside the averaging window
            while (!rpm_buf.empty() &&
                   (now - rpm_buf.front().t_ms) > RPM_AVG_WINDOW_MS) {
              rpm_buf.pop_front();
            }

            // No valid RPM → downstream must suppress PGN transmit
            if (rpm_buf.empty()) {
              return NAN;
            }

            float sum = 0.0f;
            for (const auto& s : rpm_buf) {
              sum += s.rad_s;
            }

            return sum / rpm_buf.size();
          },
          "/config/sensors/rpm/rad_per_sec_smooth"
      )
  );

  // ---------------------------------------------------------------------------
  // 5. rad/s → RPM (smoothed, internal/debug use)
  // ---------------------------------------------------------------------------
  auto* rpm_value = g_engine_rad_s_smooth->connect_to(
      new LambdaTransform<float, float>(
          [](float rad_s) {
            return std::isnan(rad_s)
                     ? NAN
                     : (rad_s * 60.0f / (2.0f * M_PI));
          },
          "/config/sensors/rpm/rpm_value"
      )
  );

#if ENABLE_DEBUG_OUTPUTS
  g_frequency->connect_to(
      new SKOutputFloat("debug.engine.revolutions_hz")
  );

  g_engine_rad_s->connect_to(
      new SKOutputFloat("debug.engine.revolutions_rad_s_raw")
  );

  g_engine_rad_s_smooth->connect_to(
      new SKOutputFloat("debug.engine.revolutions_rad_s_smooth")
  );

  rpm_value->connect_to(
      new SKOutputFloat("debug.engine.rpm")
  );
#endif

  // ---------------------------------------------------------------------------
  // 6. Signal K output (smoothed rad/s)
  // ---------------------------------------------------------------------------
  auto* sk_revs = new SKOutputFloat(
      "propulsion.engine.revolutions",
      "/config/outputs/sk/revolutions"
  );

  g_engine_rad_s_smooth->connect_to(sk_revs);

  ConfigItem(sk_revs)
      ->set_title("Engine Revolutions (rad/s)")
      ->set_description(
          "Smoothed propulsion.engine.revolutions (rad/s), suitable for "
          "NMEA 2000 PGN 127488 rapid update"
      );
}
