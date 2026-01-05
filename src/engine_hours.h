// engine_hours.h
#pragma once

/*
 * ============================================================================
 * EngineHours — Timer-driven engine runtime accumulator (SensESP v3.1.1)
 * ============================================================================
 *
 * AUTHORITATIVE RULES
 * -------------------
 *  - Canonical input: engine speed in revolutions per second (rev/s)
 *  - Engine running threshold: RPM >= 500  → (rev/s >= 500/60)
 *  - rev/s == NaN → ignore sample (do not change running state)
 *
 * DESIGN (FIXED)
 * --------------
 *  - RPM/rev-s input provides STATE only (latched last known speed)
 *  - Accumulation is driven by a 1 Hz wall-clock timer (SensESP event loop)
 *  - No dependency on RPM update frequency (avoids "steady RPM" starvation)
 *
 * OUTPUT
 * ------
 *  - Emits accumulated engine hours (float, hours)
 *  - Conversion to seconds is handled downstream (Signal K / N2K)
 *
 * PERSISTENCE
 * -----------
 *  - Stored in ESP32 NVS (Preferences)
 *  - Periodic writes to limit flash wear
 * ============================================================================
 */

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>

#include <sensesp_app.h>                 // SensESPBaseApp + event_loop()
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
    debug_hours_   = new SKOutputFloat("debug.engine.hours");
    debug_running_ = new SKOutputBool("debug.engine.running");
    debug_rps_     = new SKOutputFloat("debug.engine.revolutions_hz");  // rev/s
#endif

    // ------------------------------------------------------------------------
    // Authoritative 1 Hz wall-clock timer (SensESP-managed)
    // ------------------------------------------------------------------------
    event_loop()->onRepeat(TICK_INTERVAL_MS, [this]() {
      const unsigned long now = millis();

      // First tick establishes timebase only
      if (last_tick_ms_ == 0) {
        last_tick_ms_ = now;
        last_save_ms_ = now;
        return;
      }

      // Determine running state from last known rev/s (latched)
      if (!std::isnan(last_rev_s_) && last_rev_s_ >= REV_S_RUNNING_THRESHOLD) {
        engine_running_ = true;
      } else if (!std::isnan(last_rev_s_)) {
        engine_running_ = false;
      }
      // If last_rev_s_ is NaN: keep previous engine_running_ (ignore sample)

      if (engine_running_) {
        hours_ += (now - last_tick_ms_) / 3600000.0f;
      }

      last_tick_ms_ = now;

      // Emit RAW hours (rounding belongs downstream)
      emit(hours_);

#if ENABLE_DEBUG_OUTPUTS
      debug_hours_->set(hours_);
      debug_running_->set(engine_running_);
      debug_rps_->set(last_rev_s_);
#endif

      if (now - last_save_ms_ >= SAVE_INTERVAL_MS) {
        save_hours();
        last_save_ms_ = now;
      }
    });
  }

  ~EngineHours() {
    // Not strictly required, but polite.
    prefs_.end();
  }

  // --------------------------------------------------------------------------
  // rev/s input — state only (latched)
  // --------------------------------------------------------------------------
  void set(const float& rev_s) override {
    if (std::isnan(rev_s)) {
      // Ignore invalid samples completely (do not change state)
      return;
    }
    last_rev_s_ = rev_s;
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
  static constexpr float RPM_RUNNING_THRESHOLD     = 500.0f;
  static constexpr float REV_S_RUNNING_THRESHOLD   = RPM_RUNNING_THRESHOLD / 60.0f; // 8.33333...
  static constexpr unsigned long TICK_INTERVAL_MS  = 1000;   // 1 Hz
  static constexpr unsigned long SAVE_INTERVAL_MS  = 10000;  // flash protection

  // --------------------------------------------------------------------------
  // State
  // --------------------------------------------------------------------------
  float hours_ = 0.0f;

  float last_rev_s_ = NAN;   // latched rev/s (canonical input)
  bool  engine_running_ = false;

  unsigned long last_tick_ms_ = 0;
  unsigned long last_save_ms_ = 0;

  Preferences prefs_;

#if ENABLE_DEBUG_OUTPUTS
  SKOutputFloat* debug_hours_   = nullptr;
  SKOutputBool*  debug_running_ = nullptr;
  SKOutputFloat* debug_rps_     = nullptr;
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