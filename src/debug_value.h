#pragma once

#include "sensesp/transforms/transform.h"
#include "sensesp/ui/config_item.h"

namespace sensesp {

// A simple transform that exposes the last value in the UI.
class DebugValue : public Transform<float, float> {
 public:
  DebugValue(const String& title, const String& config_path = "")
      : Transform<float, float>(config_path), title_(title) {}

  void set(const float& value) override {
    last_value_ = value;
    emit(value);
  }

  bool to_json(JsonObject& json) override {
    json["value"] = last_value_;
    return true;
  }

  bool from_json(const JsonObject&) override {
    return true;  // read-only
  }

  String get_title() const { return title_; }

 private:
  float last_value_ = NAN;
  String title_;
};

// Provide ConfigSchema
inline String ConfigSchema(const DebugValue&) {
  return R"({
    "type": "object",
    "properties": {
      "value": { "title": "Value", "type": "number", "readOnly": true }
    }
  })";
}

}  // namespace sensesp
