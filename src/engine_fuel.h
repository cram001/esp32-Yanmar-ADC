#pragma once

/*
 * ============================================================================
 * ENGINE FUEL FLOW ESTIMATION — STABILIZED
 * ============================================================================
 *
 * RULES
 * -----
 *  • Fuel flow is a STATE, not a measurement
 *  • While engine is running:
 *      - fuel is NEVER NaN
 *      - short RPM glitches are ignored
 *  • Engine off → fuel = 0
 *
 *  • RPM input: rev/s (smoothed upstream)
 * ============================================================================
 */

#include <Arduino.h>
#include <cmath>
#include <set>

#include <sensesp/system/valueproducer.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;// engine_fuel.h
#pragma once

/*
 * ============================================================================
 * ENGINE FUEL FLOW ESTIMATION (AUTHORITATIVE)
 * ============================================================================
 *
 * PURPOSE
 * -------
 *  • Estimate engine fuel flow from RPM and vessel operating conditions
 *
 * PUBLISHES
 * ---------
 *  • propulsion.engine.fuel.rate   (m³/s, Signal K)
 *
 * CONTRACT
 * --------
 *  • Input RPM must be in revolutions per second (rev/s)
 *  • RPM is assumed smoothed upstream
 *  • Fuel output is NEVER NAN
 *      - engine off  → 0.0
 *      - engine on   → finite ≥ idle
 *
 * NOTES
 * -----
 *  • Engine load is NOT computed here
 *  • Load is handled exclusively in engine_load.h
 * ============================================================================
 */

#include <Arduino.h>
#include <cmath>
#include <set>

#include <sensesp/system/valueproducer.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;

// ============================================================================
// CONSTANTS
// ============================================================================
constexpr float DOCK_SPEED_KTS     = 0.15f;
constexpr float ENGINE_RUNNING_RPM = 500.0f;
constexpr float MS_TO_KTS          = 1.94384449f;

// ============================================================================
// HELPERS
// ============================================================================
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline bool engine_running(float rpm) {
  return !std::isnan(rpm) && rpm >= ENGINE_RUNNING_RPM;
}

// ============================================================================
// CONFIG — USE STW FLAG
// ============================================================================
static Linear* use_stw_cfg = nullptr;

// ============================================================================
// CURVES
// ============================================================================

// Baseline STW vs RPM
static std::set<CurveInterpolator::Sample> baseline_stw_curve = {
  {500,0.0},{1000,0.0},{1800,2.5},{2000,4.3},
  {2400,5.0},{2800,6.4},{3200,7.3},
  {3600,7.45},{3800,7.6},{3900,7.6}
};

// Smoothed baseline fuel curve (anchors preserved)
static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {  500, 0.60 },
  { 1000, 0.75 },
  { 1800, 1.10 },

  { 2000, 1.50 },   // fixed
  { 2200, 1.85 },
  { 2400, 2.25 },
  { 2500, 2.60 },   // fixed

  { 2800, 3.60 },
  { 3000, 4.40 },
  { 3200, 5.30 },

  { 3600, 6.90 },   // fixed
  { 3800, 7.60 },
  { 3900, 9.60 }
};

// Rated fuel curve (caps only)
static std::set<CurveInterpolator::Sample> rated_fuel_curve = {
  {500,0.9},{1800,1.4},{2000,1.8},{2400,2.45},
  {2800,3.8},{3200,5.25},{3600,7.8},{3900,9.6}
};

// Idle fuel curve
static std::set<CurveInterpolator::Sample> idle_fuel_curve = {
  { 800,  0.6 },
  { 3600, 1.4 }
};

// ============================================================================
// STW SANITY CHECK
// ============================================================================
static inline bool stw_invalid(float rpm, float stw_kts) {
  if (rpm > 3000 && stw_kts < 4.0f) return true;
  if (rpm > 2500 && stw_kts < 3.5f) return true;
  if (rpm > 1000 && stw_kts < 1.0f) return true;
  return false;
}

// ============================================================================
// WIND LOAD MULTIPLIER
// ============================================================================
static inline float wind_load_factor(float aws_kts, float awa_rad) {

  if (std::isnan(aws_kts) || std::isnan(awa_rad) || aws_kts < 7.0f) {
    return 1.0f;
  }

  constexpr float PI_F = 3.14159265f;

  float angle = clamp_val(fabsf(awa_rad), 0.0f, PI_F);
  float aws_c = clamp_val(aws_kts, 7.0f, 30.0f);

  float head  = cosf(angle);
  float cross = sinf(angle);

  float strength = (aws_c - 7.0f) / 23.0f;
  float penalty = 0.0f;

  if (head > 0.0f) penalty += strength * head * 0.30f;
  else             penalty += strength * head * 0.12f;

  penalty += strength * cross * 0.18f;

  return clamp_val(1.0f + penalty, 0.90f, 1.45f);
}

// ============================================================================
// SETUP — ENGINE FUEL
// ============================================================================
inline Transform<float,float>* setup_engine_fuel(
  ValueProducer<float>* rpm_rev_s,
  ValueProducer<float>* stw_ms,
  ValueProducer<float>* sog_ms,
  ValueProducer<float>* aws_ms,
  ValueProducer<float>* awa_rad
) {
  if (!rpm_rev_s) return nullptr;

  // Config UI
  if (!use_stw_cfg) {
    use_stw_cfg = new Linear(1.0f, 0.0f, "/config/engine_fuel/use_stw");
    ConfigItem(use_stw_cfg)
      ->set_title("Use STW for fuel model")
      ->set_description(">= 0.5 = use STW, < 0.5 = ignore STW");
  }

  // rev/s → RPM
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      if (std::isnan(rps)) return NAN;
      float r = rps * 60.0f;
      return (r < 0.0f || r > 4500.0f) ? NAN : r;
    })
  );

  // Curves
  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel  = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);
  auto* idle_fuel  = new CurveInterpolator(&idle_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);
  rpm->connect_to(idle_fuel);

  // -------------------------------------------------------------------------
  // Fuel model (L/h) — NEVER NAN
  // -------------------------------------------------------------------------
  auto* fuel_lph_raw = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      // Engine off → zero fuel
      if (!engine_running(r)) {
        return 0.0f;
      }

      float stw_kts = stw_ms ? stw_ms->get() * MS_TO_KTS : NAN;
      float sog_kts = sog_ms ? sog_ms->get() * MS_TO_KTS : NAN;

      bool use_stw = (use_stw_cfg->get() >= 0.5f);
      bool stw_valid = !std::isnan(stw_kts);
      bool sog_valid = !std::isnan(sog_kts);

      bool vessel_docked = false;
      if (use_stw) {
        if ((stw_valid && stw_kts < DOCK_SPEED_KTS) ||
            (sog_valid && sog_kts < DOCK_SPEED_KTS)) {
          vessel_docked = true;
        }
      } else {
        if (sog_valid && sog_kts < DOCK_SPEED_KTS) {
          vessel_docked = true;
        }
      }

      // Dock / idle
      if (vessel_docked) {
        float idle = idle_fuel->get();
        if (std::isnan(idle) || idle <= 0.0f) idle = 0.6f;
        return idle;
      }

      // Underway
      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f) {
        return 0.6f;
      }

      float stw_factor = 1.0f;
      if (use_stw && stw_valid && !stw_invalid(r, stw_kts) &&
          !std::isnan(baseSTW) && baseSTW > DOCK_SPEED_KTS) {

        float ratio = baseSTW / stw_kts;
        if (ratio >= 1.05f) {
          stw_factor = clamp_val(powf(ratio, 0.6f), 1.0f, 2.0f);
        }
      }

      float wind_factor = 1.0f;
      if (aws_ms && awa_rad) {
        wind_factor = wind_load_factor(
          aws_ms->get() * MS_TO_KTS,
          awa_rad->get()
        );
      }

      float fuel = baseFuel * stw_factor * wind_factor;
      if (!std::isnan(fuelMax) && fuel > fuelMax) fuel = fuelMax;

      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  // Smooth for display only
  auto* fuel_lph = fuel_lph_raw->connect_to(new MovingAverage(2));

  // Publish to Signal K (m³/s)
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return (lph <= 0.0f) ? 0.0f : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  // IMPORTANT: return RAW fuel (L/h) for engine_load.h
  return fuel_lph_raw;
}

// ============================================================================
// CONSTANTS
// ============================================================================
constexpr float ENGINE_RUNNING_RPM = 500.0f;
constexpr float DOCK_SPEED_KTS     = 0.15f;
constexpr float MS_TO_KTS          = 1.94384449f;

// RPM glitch tolerance
constexpr unsigned long RPM_HOLD_MS = 700;

// ============================================================================
// HELPERS
// ============================================================================
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
// CURVES
// ============================================================================
static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {  500, 0.60 },
  { 1000, 0.75 },
  { 1800, 1.10 },
  { 2000, 1.50 },
  { 2200, 1.85 },
  { 2400, 2.25 },
  { 2500, 2.60 },
  { 2800, 3.60 },
  { 3000, 4.40 },
  { 3200, 5.30 },
  { 3600, 6.90 },
  { 3800, 7.60 },
  { 3900, 9.60 }
};

static std::set<CurveInterpolator::Sample> idle_fuel_curve = {
  { 800, 0.6 },
  { 3600, 1.4 }
};

// ============================================================================
// CONFIG
// ============================================================================
static Linear* use_stw_cfg = nullptr;

// ============================================================================
// SETUP — ENGINE FUEL (STABILIZED)
// ============================================================================
inline Transform<float,float>* setup_engine_fuel(
  ValueProducer<float>* rpm_rev_s,
  ValueProducer<float>* stw_ms,
  ValueProducer<float>* sog_ms
) {
  if (!rpm_rev_s) return nullptr;

  // Config UI
  if (!use_stw_cfg) {
    use_stw_cfg = new Linear(1.0f, 0.0f, "/config/engine_fuel/use_stw");
    ConfigItem(use_stw_cfg)
      ->set_title("Use STW for fuel model");
  }

  // rev/s → RPM
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      return std::isnan(rps) ? NAN : rps * 60.0f;
    })
  );

  auto* base_fuel = new CurveInterpolator(&baseline_fuel_curve);
  auto* idle_fuel = new CurveInterpolator(&idle_fuel_curve);

  rpm->connect_to(base_fuel);
  rpm->connect_to(idle_fuel);

  // -------------------------------------------------------------------------
  // STABILIZED fuel model (L/h)
  // -------------------------------------------------------------------------
  auto* fuel_lph_raw = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      static float last_fuel = 0.0f;
      static unsigned long last_valid_rpm_ms = 0;
      static bool engine_running = false;

      unsigned long now = millis();

      // Valid RPM?
      if (!std::isnan(r) && r >= ENGINE_RUNNING_RPM) {
        engine_running = true;
        last_valid_rpm_ms = now;
      }

      // RPM missing but within hold window → keep running
      if (engine_running && (now - last_valid_rpm_ms) < RPM_HOLD_MS) {
        // still running
      } else if (std::isnan(r) || r < ENGINE_RUNNING_RPM) {
        engine_running = false;
        last_fuel = 0.0f;
        return 0.0f;
      }

      // Engine running → compute fuel
      float fuel = NAN;

      bool docked = false;
      if (use_stw_cfg->get() >= 0.5f) {
        float stw = stw_ms ? stw_ms->get() * MS_TO_KTS : NAN;
        float sog = sog_ms ? sog_ms->get() * MS_TO_KTS : NAN;
        if ((!std::isnan(stw) && stw < DOCK_SPEED_KTS) ||
            (!std::isnan(sog) && sog < DOCK_SPEED_KTS)) {
          docked = true;
        }
      }

      if (docked) {
        fuel = idle_fuel->get();
      } else {
        fuel = base_fuel->get();
      }

      if (std::isnan(fuel) || fuel <= 0.0f) {
        fuel = last_fuel;   // HOLD LAST GOOD VALUE
      }

      last_fuel = fuel;
      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  // Smooth for display only
  auto* fuel_lph = fuel_lph_raw->connect_to(new MovingAverage(2));

  // Signal K output (m³/s) — NEVER NaN
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return (lph <= 0.0f) ? 0.0f : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  return fuel_lph_raw;
}
