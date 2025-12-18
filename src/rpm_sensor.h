/* Calculates engine RPM based on signal tapped from engine's magnetic RPM sender
Requires OpAmp to amplify the signal and optocoupler to electrically isolate the signal 
from the ESP32

Note that OpAmp may require 5V, which is not provided on the ESP32 board */

#pragma once

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

// Shared RPM signal (rev/s)
extern Frequency* g_frequency;

// -----------------------------------------------------------------------------
// RPM sensor (magnetic pickup / flywheel teeth)
// -----------------------------------------------------------------------------
inline void setup_rpm_sensor() {

  // ---------------------------------------------------------------------------
  // 1. Pulse counter (interrupt-driven)
  // ---------------------------------------------------------------------------
  auto* pulse_input = new DigitalInputCounter(
      PIN_RPM,
      INPUT,
      CHANGE,
      0      // no auto-reset
  );

  // ---------------------------------------------------------------------------
  // 2. Pulses/sec → revolutions/sec
  // ---------------------------------------------------------------------------
  g_frequency = pulse_input->connect_to(
      new Frequency(
          1.0f / RPM_TEETH,
          "/config/sensors/rpm/frequency"
      )
  );

  // ---------------------------------------------------------------------------
  // 3. Revolutions/sec → RPM (human readable)
  // ---------------------------------------------------------------------------
  auto* rpm_value = g_frequency->connect_to(
      new LambdaTransform<float, float>(
          [](float rev_per_sec) {
            return rev_per_sec * 60.0f;
          },
          "/config/sensors/rpm/rpm_value"
      )
  );

  // ---------------------------------------------------------------------------
  // 4. Signal K output (RPM)
  // ---------------------------------------------------------------------------
  auto* sk_rpm = new SKOutputFloat(
      "propulsion.engine.revolutions",
      "/config/outputs/sk/rpm"
  );

  // Publish directly from g_frequency (avoids double scaling)
  g_frequency->connect_to(sk_rpm);

  // ---------------------------------------------------------------------------
  // 5. Debug outputs (optional)
  // ---------------------------------------------------------------------------
#if ENABLE_DEBUG_OUTPUTS
  rpm_value->connect_to(
      new SKOutputFloat("debug.rpm_readable")
  );

  g_frequency->connect_to(
      new SKOutputFloat("debug.rpm_hz")
  );
#endif

  // ---------------------------------------------------------------------------
  // 6. Config UI
  // ---------------------------------------------------------------------------
  ConfigItem(sk_rpm)
      ->set_title("RPM SK Path")
      ->set_description("Signal K path for engine RPM")
      ->set_sort_order(500);
}
