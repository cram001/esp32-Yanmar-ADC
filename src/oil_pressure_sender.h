#pragma once

#include <set>
#include <cmath>

#include "sensesp/transforms/lambda_transform.h"
#include "sensesp/transforms/median.h"
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/ui/config_item.h"
#include "calibrated_analog_input.h"

namespace sensesp {

// -----------------------------------------------------------------------------
// Oil Pressure Sender Pipeline
//  - Resistive sender to ground (no gauge present)
//  - Sender presence detection via divider physics
//  - Outputs Pa (Signal K compliant)
// -----------------------------------------------------------------------------
class OilPressureSender {
 public:
  OilPressureSender(uint8_t adc_pin,
                    float adc_ref_voltage,
                    float pullup_resistance,
                    float sample_rate_hz,
                    const String& sk_path,
                    const String& config_path) {

    // ---------------------------
    // Thresholds for detection
    // ---------------------------
    const float open_threshold  = 0.95f * adc_ref_voltage;
    const float short_threshold = 0.05f * adc_ref_voltage;

    // ---------------------------
    // 1) ADC input (calibrated)
    // ---------------------------
    auto* adc = new CalibratedAnalogInput(
        adc_pin,
        sample_rate_hz,
        config_path + "/adc_raw"
    );

    // ---------------------------
    // 2) Median filter
    // ---------------------------
    auto* adc_smooth = adc->connect_to(
        new Median(5, config_path + "/adc_smooth")
    );

    // ---------------------------
    // 3) Voltage → Resistance
    // ---------------------------
    auto* resistance = adc_smooth->connect_to(
        new LambdaTransform<float, float>(
            [=](float v) -> float {

              // Sender not present or wiring fault
              if (v > open_threshold)  return NAN;
              if (v < short_threshold) return NAN;

              // Divider inversion
              return (v * pullup_resistance) /
                     (adc_ref_voltage - v);
            },
            config_path + "/sender_resistance"
        )
    );

    // ---------------------------
    // 4) Resistance → PSI curve
    // ---------------------------
    static std::set<CurveInterpolator::Sample> ohms_to_psi = {
        {240, 0},
        {200, 10},
        {160, 20},
        {140, 30},
        {120, 40},
        {100, 50},
        {90,  60},
        {75,  70},
        {60,  80},
        {45,  90},
        {33, 100}
    };

    auto* psi = resistance->connect_to(
        new CurveInterpolator(
            &ohms_to_psi,
            config_path + "/psi"
        )
    );

    // ---------------------------
    // 5) PSI → Pascals
    // ---------------------------
    auto* pressure_pa = psi->connect_to(
        new Linear(
            6894.76f,
            0.0f,
            config_path + "/pa"
        )
    );

    // ---------------------------
    // 6) Signal K output
    // ---------------------------
    auto* sk_output = new SKOutputFloat(
        sk_path,
        config_path + "/sk_output"
    );

    pressure_pa->connect_to(sk_output);

    // ---------------------------
    // 7) UI
    // ---------------------------
    ConfigItem(sk_output)
        ->set_title("Oil Pressure SK Path")
        ->set_description("Signal K path for engine oil pressure")
        ->set_sort_order(550);

#if ENABLE_DEBUG_OUTPUTS
    adc->connect_to(new SKOutputFloat("debug.oil.adc_volts"));
    resistance->connect_to(new SKOutputFloat("debug.oil.sender_resistance"));
    psi->connect_to(new SKOutputFloat("debug.oil.psi"));
#endif
  }
};

}  // namespace sensesp
