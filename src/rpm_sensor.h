#pragma once

#include <cmath>

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
extern const float RPM_TEETH;

// Shared signals
extern Frequency* g_frequency;                 // Hz (rev/s)
extern ValueProducer<float>* g_engine_rad_s;   // rad/s

// -----------------------------------------------------------------------------
// RPM sensor (magnetic pickup / flywheel teeth)
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
  // 3. rev/s → rad/s (canonical Signal K value)
  // ---------------------------------------------------------------------------
  g_engine_rad_s = g_frequency->connect_to(
      new LambdaTransform<float, float>(
          [](float rev_s) {
            return rev_s * 2.0f * M_PI;
          },
          "/config/sensors/rpm/rad_per_sec"
      )
  );

  // ---------------------------------------------------------------------------
  // 4. rad/s → RPM (kept for internal / future use)
  // ---------------------------------------------------------------------------
  auto* rpm_value = g_engine_rad_s->connect_to(
      new LambdaTransform<float, float>(
          [](float rad_s) {
            return rad_s * 60.0f / (2.0f * M_PI);
          },
          "/config/sensors/rpm/rpm_value"
      )
  );

#if ENABLE_DEBUG_OUTPUTS
  g_frequency->connect_to(
      new SKOutputFloat("debug.engine.revolutions_hz")
  );

  g_engine_rad_s->connect_to(
      new SKOutputFloat("debug.engine.revolutions_rad_s")
  );

  rpm_value->connect_to(
      new SKOutputFloat("debug.engine.rpm")
  );
#endif

  // ---------------------------------------------------------------------------
  // 5. Signal K output (rad/s)
  // ---------------------------------------------------------------------------
  auto* sk_revs = new SKOutputFloat(
      "propulsion.engine.revolutions",
      "/config/outputs/sk/revolutions"
  );

  g_engine_rad_s->connect_to(sk_revs);

  ConfigItem(sk_revs)
      ->set_title("Engine Revolutions (rad/s)")
      ->set_description("Signal K propulsion.engine.revolutions output");
}
