#pragma once

#include "sensesp/transforms/transform.h"
#include "sensesp/ui/config_item.h"
#include <Preferences.h>

namespace sensesp {

class EngineHours : public Transform<float, float> {
 public:
  EngineHours(const String& config_path = "")
      : Transform<float, float>(config_path) {

    prefs_.begin("engine_runtime", false);
    load_hours();
  // IMPORTANT:
   // Emit stored value immediately so UI shows it even when RPM = 0
  float rounded = float(int(hours_ * 10)) / 10.0f;
  emit(rounded);
  }

  // v3.x override: this is the correct hook
  void set(const float& rpm) override {
    unsigned long now = millis();

    if (last_update_ != 0 && rpm > 500.0f) {
      float dt_hours = (now - last_update_) / 3600000.0f;
      hours_ += dt_hours;
      save_hours();
    }

    last_update_ = now;

    float rounded = float(int(hours_ * 10)) / 10.0f;
    emit(rounded);
  }

  // ---- Config persistence ----
  bool to_json(JsonObject& json) override {
    json["hours"] = hours_;
    return true;
  }

  bool from_json(const JsonObject& json) override {
    if (json["hours"].is<float>()) {
      hours_ = json["hours"].as<float>();
      save_hours();
    }
    return true;
  }

 private:
  float hours_ = 0.0f;
  unsigned long last_update_ = 0;
  Preferences prefs_;

  void load_hours() {
    hours_ = prefs_.getFloat("engine_hours", 0.0f);
  }

  void save_hours() {
    prefs_.putFloat("engine_hours", hours_);
  }
};

// Required for SensESP config UI
inline String ConfigSchema(const EngineHours&) {
  return R"({
    "type": "object",
    "properties": {
      "hours": { "title": "Engine Hours", "type": "number" }
    }
  })";
}

} // namespace sensesp
