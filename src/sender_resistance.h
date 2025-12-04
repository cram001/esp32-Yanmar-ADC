#pragma once

#include "sensesp/transforms/transform.h"
#include <cmath>

/**
 * SenderResistance
 *
 * Computes the actual coolant sender resistance by:
 *   1. Reversing the DFRobot 30k/7.5k voltage divider
 *   2. Applying gauge-series Ohm’s law:
 *
 *         +12V → Rgauge → Rsender → GND
 *
 *         Vtap = Vs * (Rsender / (Rgauge + Rsender))
 *
 *         Rsender = Rgauge * (Vtap / (Vs - Vtap))
 */
class SenderResistance : public sensesp::Transform<float, float> {

 public:
  SenderResistance(float supply_voltage, float r_gauge_coil)
      : Transform<float, float>("sender_resistance"),
        supply_voltage_(supply_voltage),
        r_gauge_coil_(r_gauge_coil) {}

  void set(const float& vadc) override {

    // ============================================================
    // 1. Handle invalid ADC inputs
    // ============================================================
    if (std::isnan(vadc) || std::isinf(vadc)) {
      emit(NAN);
      return;
    }

    // Floating input / no signal
    if (vadc < 0.01f) {
      emit(NAN);
      return;
    }

    // ============================================================
    // 2. Reverse the DFRobot 30k/7.5k divider
    //
    //    Vtap_actual = Vadc * (R1 + R2) / R2
    //                 = Vadc * (30000 + 7500) / 7500
    //                 = Vadc * 5.0
    //
    // ============================================================
    constexpr float DIVIDER_SCALE = 5.0f;
    float Vtap = vadc * DIVIDER_SCALE;

    if (Vtap <= 0.0f) {
      emit(NAN);
      return;
    }

    // If Vtap > Vsupply, impossible
    if (Vtap >= supply_voltage_ - 0.05f) {
      emit(NAN);
      return;
    }

    // ============================================================
    // 3. Compute Rsender from gauge-series formula
    //
    //    Rsender = Rgauge * (Vtap / (Vs - Vtap))
    //
    // ============================================================
    float denom = supply_voltage_ - Vtap;

    if (denom <= 0.01f) {
      emit(NAN);
      return;
    }

    float Rsender = r_gauge_coil_ * (Vtap / denom);

    // ============================================================
    // 4. Sanity checks on sender resistance
    // ============================================================
    if (Rsender < 20.0f) {
      // Short / wiring fault
      emit(NAN);
      return;
    }

    if (Rsender > 20000.0f) {
      // Open circuit or impossible
      emit(NAN);
      return;
    }

    // All good
    emit(Rsender);
  }

 private:
  float supply_voltage_;     // battery/gauge supply voltage (12.0–14.4)
  float r_gauge_coil_;       // gauge internal resistance (~1180 Ω)
};
