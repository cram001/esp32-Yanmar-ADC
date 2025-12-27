#pragma once

/*
 * ============================================================================
 * EngineHours
 * ============================================================================
 *
 * Purpose
 * -------
 * Accumulates engine run time in HOURS based on engine speed updates.
 *
 * Design principles
 * -----------------
 *  - Time-based accumulation (NOT update-rate dependent)
 *  - Any finite RPM > 0 means "engine running" (idle counts)
 *  - No arbitrary RPM threshold (e.g. >500 RPM)
 *  - Decoupled timing:
 *      * accumulation → continuous via millis()
 *      * output        → ~1 Hz
 *      * persistence   → low frequency to limit flash wear
 *
 * Units
 * -----
 *  - Input:  RPM (float)
 *  - Stored internally: hours (float, authoritative)
 *  - Output (emit): hours, rounded to 0.1 h
 *
 * Signal K
 * --------
 *  - This class outputs HOURS.
 *  - Conversion to seconds for Signal K is handled externally.
 *
 * Debug
 * -----
 *  - Optional SK debug output publishes raw, unrounded hours
 *    to allow validation of accumulation and persistence behavior.
 *
 * Explicit non-goals
 * ------------------
 *  - No UI coupling (StatusPageItem)
 *  - No RPM gating logic
 *  - No flash writes on every update
 *  - No dependency on engine load or vessel movement
 *
 * Notes
 * -----
 *  - If an explicit "engine running" boolean is desired in the future,
 *    derive it upstream and feed it here instead of RPM.
 * ============================================================================
 */

#include <Arduino.h>
#include <Preferences.h>

#include <sensesp/transforms/transform.h>
#include <sensesp/signalk/signalk_output.h>

namespace sensesp {

class EngineHours : public Transform<float, float> {
 public:
  explicit EngineHours(const String& config_path = "")
      : Transform<float, float>(config_path) {

    // Open NVS namespace for persistent storage
    prefs_.begin("engine_runtime", false);

    // Restore previously stored engine hours (if any)
    load_hours();

#if ENABLE_DEBUG_OUTPUTS
    // Raw (unrounded) hours for debugging / validation
    debug_hours_ = new SKOutputFloat("debug.engine.hours");
#endif
  }

  // --------------------------------------------------------------------------
  // SensESP v3.x hook
  // --------------------------------------------------------------------------
  void set(const float& rpm) override {
    const unsigned long now = millis();

    // Any finite positive RPM means the engine is turning
    bool engine_running = (!isnan(rpm) && rpm > 0.0f);

    // ------------------------------------------------------------
    // 1. Accumulate engine hours (time-based)
    // ------------------------------------------------------------
    // IMPORTANT:
    // Accumulation is based on wall-clock time, NOT update frequency.
    // This ensures engine hours continue to increment even if RPM
    // updates stall or remain constant.
    if (last_sample_ms_ != 0 && engine_running) {
      float dt_hours = (now - last_sample_ms_) / 3600000.0f;
      hours_ += dt_hours;
    }

    // Always advance sample clock
    last_sample_ms_ = now;

    // ------------------------------------------------------------
    // 2. Emit output at a controlled rate (~1 Hz)
    // ------------------------------------------------------------
    if (now - last_emit_ms_ >= EMIT_INTERVAL_MS) {
      emit(round_hours(hours_));

#if ENABLE_DEBUG_OUTPUTS
      if (debug_hours_) {
        debug_hours_->set(hours_);  // raw, unrounded
      }
#endif
      last_emit_ms_ = now;
    }

    // ------------------------------------------------------------
    // 3. Persist occasionally (flash wear protection)
    // ------------------------------------------------------------
    if (now - last_save_ms_ >= SAVE_INTERVAL_MS) {
      save_hours();
      last_save_ms_ = now;
    }
  }

  // --------------------------------------------------------------------------
  // Configuration persistence (SensESP UI)
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
  // Timing constants
  static constexpr unsigned long EMIT_INTERVAL_MS = 1000;   // ~1 Hz output
  static constexpr unsigned long SAVE_INTERVAL_MS = 10000;  // flash protection

  // Accumulated engine hours (authoritative value)
  float hours_ = 0.0f;

  // Timing state
  unsigned long last_sample_ms_ = 0;
  unsigned long last_emit_ms_   = 0;
  unsigned long last_save_ms_   = 0;

  // Persistent storage
  Preferences prefs_;

#if ENABLE_DEBUG_OUTPUTS
  // Raw debug output (unrounded)
  SKOutputFloat* debug_hours_ = nullptr;
#endif

  // Round to 0.1 h for user-facing output
  static float round_hours(float h) {
    return floorf(h * 10.0f) / 10.0f;
  }

  void load_hours() {
    hours_ = prefs_.getFloat("engine_hours", 0.0f);
  }

  void save_hours() {
    prefs_.putFloat("engine_hours", hours_);
  }
};

// --------------------------------------------------------------------------
// SensESP configuration schema (REQUIRED)
// --------------------------------------------------------------------------
inline String ConfigSchema(const EngineHours&) {
  return R"JSON({
    "type": "object",
    "properties": {
      "hours": {
        "title": "Engine Hours",
        "type": "number",
        "description": "Engine run time accumulator (stored in hours; converted to seconds for Signal K)"
      }
    }
  })JSON";
}

}  // namespace sensesp
