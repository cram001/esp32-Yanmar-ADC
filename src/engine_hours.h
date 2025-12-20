#pragma once

/*
 * ============================================================================
 * FILE: engine_hours.h
 *
 * PURPOSE
 * -------
 *  Persistent engine hours counter for SensESP / Signal K systems.
 *
 *  • Receives engine RPM as input
 *  • Accumulates runtime ONLY when engine is running
 *  • Persists total hours using ESP32 NVS (Preferences)
 *  • Publishes engine hours EVEN WHEN THE ENGINE IS OFF
 *
 * DESIGN NOTE (IMPORTANT)
 * ----------------------
 *  This transform relies on the RPM source emitting at startup (RPM = 0),
 *  which is how SensESP RPM / SK listeners behave.
 *
 *  Therefore:
 *   • No delayed emit is required
 *   • No ReactESP or scheduler dependency is needed
 *
 *  Engine hours are emitted from set(), even when RPM = 0.
 *
 * SIGNAL K NOTE
 * -------------
 *  This transform outputs HOURS.
 *  Conversion to seconds for propulsion.engine.runTime MUST be done in main.cpp.
 *
 * FLASH / OTA SAFETY NOTE (CRITICAL)
 * ---------------------------------
 *  Writing to ESP32 NVS (Preferences) blocks flash access.
 *  OTA updates also require exclusive flash access.
 *
 *  Therefore:
 *   • Flash writes MUST be rate-limited
 *   • Flash writes MUST NOT occur on every RPM update
 *
 *  Failure to do this WILL cause OTA to fail after ~30–60 seconds.
 * ============================================================================
 */

#include <Preferences.h>
#include <ArduinoJson.h>

#include <sensesp/transforms/transform.h>
#include <sensesp/ui/config_item.h>

namespace sensesp {

/*
 * ============================================================================
 * EngineHours Transform
 * ============================================================================
 */
class EngineHours : public Transform<float, float> {
 public:
  EngineHours(const String& config_path = "")
      : Transform<float, float>(config_path) {

    // Open NVS namespace
    prefs_.begin("engine_runtime", false);

    // Restore stored hours
    load_hours();
  }

  /*
   * SensESP v3.x input hook
   *
   * Input:
   *   rpm — engine speed in RPM
   *
   * Behavior:
   *   • Accumulates runtime only when rpm > 500
   *   • ALWAYS emits current engine hours (engine on or off)
   *
   * This guarantees engine hours are published even when RPM = 0.
   */
  void set(const float& rpm) override {
    const unsigned long now = millis();

    // -----------------------------------------------------------------------
    // Accumulate runtime ONLY when engine is running
    // -----------------------------------------------------------------------
    if (last_update_ != 0 && rpm > 500.0f) {
      const float dt_hours = (now - last_update_) / 3600000.0f;
      hours_ += dt_hours;

      // ---------------------------------------------------------------------
      // CRITICAL: Rate-limit flash writes
      // ---------------------------------------------------------------------
      // Writing to Preferences (NVS) blocks flash access and WILL break OTA
      // if done too frequently.
      //
      // We therefore:
      //  • Save at most once every SAVE_INTERVAL_MS
      //  • Still emit continuously (RAM-only operation)
      //
      static constexpr unsigned long SAVE_INTERVAL_MS = 60000;
      unsigned long last_save_ms_ = 0;

      if (now - last_save_ms_ >= SAVE_INTERVAL_MS) {
        save_hours();
        last_save_ms_ = now;
}
    }

    // -----------------------------------------------------------------------
    // Detect engine stop (falling edge) and commit one final save
    // -----------------------------------------------------------------------
    if (rpm <= 500.0f && last_rpm_ > 500.0f) {
      save_hours();
      last_save_ms_ = now;
    }

    // Update timestamps
    last_update_ = now;
    last_rpm_ = rpm;

    // ALWAYS emit stored value
    emit_rounded();
  }

  // ---------------------------------------------------------------------------
  // SensESP configuration persistence
  // ---------------------------------------------------------------------------
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
  float hours_ = 0.0f;            // Total engine hours
  unsigned long last_update_ = 0; // Last RPM update timestamp
  float last_rpm_ = 0.0f;         // Previous RPM (for edge detection)

  // -------------------------------------------------------------------------
  // Flash write rate limiting (OTA safety)
  // -------------------------------------------------------------------------
  unsigned long last_save_ms_ = 0;
  static constexpr unsigned long SAVE_INTERVAL_MS = 60000; // 60 seconds

  Preferences prefs_;             // ESP32 NVS handler

  void load_hours() {
    hours_ = prefs_.getFloat("engine_hours", 0.0f);
  }

  void save_hours() {
    prefs_.putFloat("engine_hours", hours_);
  }

  // Round to 0.1 h for UI stability
  void emit_rounded() {
    const float rounded = float(int(hours_ * 10.0f)) / 10.0f;
    emit(rounded);
  }
};

/*
 * ============================================================================
 * SensESP Config UI schema
 * ============================================================================
 */
inline String ConfigSchema(const EngineHours&) {
  return R"({
    "type": "object",
    "properties": {
      "hours": {
        "title": "Engine Hours",
        "type": "number"
      }
    }
  })";
}

}  // namespace sensesp
