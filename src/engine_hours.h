#pragma once

#include "sensesp/transforms/transform.h"
#include "sensesp/system/observable.h"
#include "sensesp/system/saveable.h"

class EngineHours : public sensesp::FloatTransform {
 public:
  EngineHours(float* initial_hours_ptr)
      : sensesp::FloatTransform(""), initial_hours_ptr_{initial_hours_ptr} {}

void set_input(const float& rpm) override {
    unsigned long now = millis();

    if (last_update_ != 0 && rpm > 50.0f) {
        float dt_hours = (now - last_update_) / 3600000.0f;
        hours_ += dt_hours;
    }

    last_update_ = now;

    // emit updated value
    this->emit(hours_);
}

 private:
  float* initial_hours_ptr_;
  float accumulated_hours_ = 0.0f;
};
