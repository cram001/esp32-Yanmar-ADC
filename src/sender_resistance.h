#pragma once

#include "sensesp/transforms/transform.h"

class SenderResistance : public sensesp::Transform<float, float> {
 public:
  SenderResistance(float supply_voltage, float rgauge)
      : Transform<float, float>("sender_resistance"),
        supply_voltage_(supply_voltage),
        rgauge_(rgauge) {}

  // THE ONE AND ONLY CORRECT OVERRIDE FOR YOUR VERSION OF SENSESP
  void set(const float& Vgauge) override {

    // Missing sender
    if (Vgauge < 0.05f) {
        this->emit(-1.0f);
        return;
    }

    // Out of range
    if (Vgauge >= supply_voltage_ || Vgauge < 0.0f) {
        this->emit(NAN);
        return;
    }

    // Compute sender resistance
    float Rsender = rgauge_ * (Vgauge / (supply_voltage_ - Vgauge));

    this->emit(Rsender);
  }

 private:
  float supply_voltage_;
  float rgauge_;
};
