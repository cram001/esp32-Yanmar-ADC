#pragma once

/*
 * ============================================================================
 * EngineHours — Timer-driven engine runtime accumulator (SensESP v3.1.1)
 * ============================================================================
 *
 * Rules (authoritative):
 *  - rpm >= 500  → engine running → accrue time
 *  - rpm < 500   → engine stopped → do not accrue
 *  - rpm == NaN  → ignore sample (no state change)
 *
 * Design:
 *  - RPM provides STATE only
 *  - Time accumulation driven by SensESP event loop (1 Hz)
 *  - No dependency on RPM update frequency
 *
 * Output:
 *  - Emits engine hours as float
 *  - Conversion to seconds handled downstream (SK / N2K)
 *
 * Persistence:
 *  - Stored in ESP32 NVS (Preferences)
 *  - Periodic writes to limit flash wear
 * ============================================================================
 */

#include <Arduino.h>
#include <Preferences.h>

#include <sensesp_app.h>                       // SensESPBaseApp + event_loop()
#include <sensesp/transforms/transform.h>
#include <sensesp/signalk/signalk_output.h>

namespace sensesp {

class EngineHours : public Transform<float, float> {
 public:
  explicit EngineHours(const String& config_path = "")
      : Transform<float, float>(config_path) {

    // ------------------------------------------------------------------------
    // Persistent storage
    // ------------------------------------------------------------------------
    prefs_.begin("engine_runtime", false);
    load_hours();

#if ENABLE_DEBUG_OUTPUTS
    debug_hours_   = new SKOutput<float>("debug.engine.hours");
    debug_running_ = new SKOutput<bool>("debug.engine.running");
#endif

    // ------------------------------------------------------------------------
    // Authoritative 1 Hz wall-clock timer (SensESP-managed)
    // ------------------------------------------------------------------------
    event_loop()->onRepeat(TICK_INTERVAL_MS, [this]() {
      const unsigned long now = millis();

      // First tick establishes timebase only
      if (last_tick_ms_ == 0) {
        last_tick_ms_ = now;
        return;
      }

      if (engine_running_) {
        hours_ += (now - last_tick_ms_) / 3600000.0f;
      }

      last_tick_ms_ = now;

      // Emit RAW hours (rounding belongs downstream)
      emit(hours_);

#if ENABLE_DEBUG_OUTPUTS
      debug_hours_->set(hours_);
      debug_running_->set(engine_running_);
#endif

      if (now - last_save_ms_ >= SAVE_INTERVAL_MS) {
        save_hours();
        last_save_ms_ = now;
      }
    });
  }

  // --------------------------------------------------------------------------
  // RPM input — state only
  // --------------------------------------------------------------------------
  void set(const float& rpm) override {
    if (isnan(rpm)) {
      // Ignore invalid samples completely
      return;
    }

    engine_running_ = (rpm >= RPM_RUNNING_THRESHOLD);
  }

  // --------------------------------------------------------------------------
  // SensESP configuration persistence
  // --------------------------------------------------------------------------
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
  // --------------------------------------------------------------------------
  // Constants
  // --------------------------------------------------------------------------
  static constexpr float RPM_RUNNING_THRESHOLD = 500.0f;
  static constexpr unsigned long TICK_INTERVAL_MS = 1000;   // 1 Hz
  static constexpr unsigned long SAVE_INTERVAL_MS = 10000;  // flash protection

  // --------------------------------------------------------------------------
  // State
  // --------------------------------------------------------------------------
  float hours_ = 0.0f;
  bool engine_running_ = false;

  unsigned long last_tick_ms_ = 0;
  unsigned long last_save_ms_ = 0;

  Preferences prefs_;

#if ENABLE_DEBUG_OUTPUTS
  SKOutput<float>* debug_hours_   = nullptr;
  SKOutput<bool>*  debug_running_ = nullptr;
#endif

  // --------------------------------------------------------------------------
  // Persistence helpers
  // --------------------------------------------------------------------------
  void load_hours() {
    hours_ = prefs_.getFloat("engine_hours", 0.0f);
  }

  void save_hours() {
    prefs_.putFloat("engine_hours", hours_);
  }
};

// --------------------------------------------------------------------------
// SensESP configuration schema (required)
// --------------------------------------------------------------------------
inline String ConfigSchema(const EngineHours&) {
  return R"JSON({
    "type": "object",
    "properties": {
      "hours": {
        "title": "Engine Hours",
        "type": "number",
        "description": "Engine run time accumulator (hours; timer-driven)"
      }
    }
  })JSON";
}

}  // namespace sensesp
