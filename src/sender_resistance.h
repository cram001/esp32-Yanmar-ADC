#pragma once

#include "sensesp/transforms/transform.h"

class SenderResistance : public sensesp::Transform<float, float> {
 public:
  SenderResistance(float supply_voltage, float rgauge)
      : supply_voltage_(supply_voltage), rgauge_(rgauge) {}

  // NOTE: DO NOT use "override". SensESP v3 signature differs internally.
void set_input(float Vgauge, uint8_t input_channel = 0) {

    // Detect missing sensor / floating ADC:
    if (Vgauge < 0.05f) {
        // Force sender resistance to "error"
        this->output_ = -1.0f;   // impossible resistance
        this->notify();
        return;
    }

    // If gauge voltage is impossible, send NaN
    if (Vgauge >= supply_voltage_ || Vgauge < 0.0f) {
        this->output_ = NAN;
        this->notify();
        return;
    }

    // Normal operation:
    float Rsender = rgauge_ * (Vgauge / (supply_voltage_ - Vgauge));

    this->output_ = Rsender;
    this->notify();
}

 private:
  float supply_voltage_;
  float rgauge_;
};
