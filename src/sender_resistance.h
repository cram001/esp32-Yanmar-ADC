#pragma once

#include "sensesp/transforms/transform.h"
#include <cmath>

class SenderResistance : public sensesp::Transform<float, float> {

 public:
  SenderResistance(float supply_voltage, float rgauge)
      : Transform<float, float>("sender_resistance"),
        supply_voltage_(supply_voltage),
        rgauge_(rgauge) {}

  void set(const float& Vgauge) override {

    // ============================================================
    // 1. Handle invalid ADC values
    // ============================================================
    if (std::isnan(Vgauge) || std::isinf(Vgauge)) {
      emit(NAN);
      return;
    }

    // A floating input or open sender will often read ~0.00–0.03V
    if (Vgauge < 0.03f) {
      // Open circuit or no coolant sender connected
      emit(NAN);
      return;
    }

    // Any voltage too close to Vsupply is physically impossible
    if (Vgauge >= (supply_voltage_ - 0.05f)) {
      // Short-to-supply, sender open, wiring fault
      emit(NAN);
      return;
    }

    // Negative voltage is always invalid
    if (Vgauge <= 0.0f) {
      emit(NAN);
      return;
    }

    // ============================================================
    // 2. Compute sender resistance
    //    Rsender = Rgauge * (Vsender / (Vsupply - Vsender))
    // ============================================================

    float denom = supply_voltage_ - Vgauge;

    // Safety: never divide by zero
    if (denom < 0.01f) {
      emit(NAN);
      return;
    }

    float Rsender = rgauge_ * (Vgauge / denom);

    // ============================================================
    // 3. Additional sanity checks
    // ============================================================

    if (Rsender < 5.0f) {
      // Shorted thermistor or wiring fault
      emit(NAN);
      return;
    }

    if (Rsender > 100000.0f) {
      // Open circuit or impossible value
      emit(NAN);
      return;
    }

    // ============================================================
    // 4. Everything OK → output ohms
    // ============================================================
    emit(Rsender);
  }

 private:
  float supply_voltage_;
  float rgauge_;
};
