// engine_fuel.h
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
constexpr float DOCK_SPEED_KTS     = 0.20f;
constexpr float ENGINE_RUNNING_RPM = 500.0f;
constexpr float MS_TO_KTS          = 1.94384449f;
static constexpr uint32_t FUEL_OUTPUT_HOLD_MS = 4000;

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

// Latch helper: hold last good value briefly to ride through transient NANs
static inline float latch_with_hold(
  float v,
  float& slot,
  uint32_t& ts_ms,
  bool zero_on_expire = false
) {
  const uint32_t now = millis();

  if (std::isfinite(v)) {
    slot  = v;
    ts_ms = now;
    return v;
  }

  if (ts_ms != 0 &&
      (now - ts_ms) <= FUEL_OUTPUT_HOLD_MS &&
      std::isfinite(slot)) {
    return slot;
  }

  slot = zero_on_expire ? 0.0f : NAN;
  return slot;
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
  {500,0.0},{1000,1.0},{1800,2.9},{2000,4.5},
  {2250,5.5},{2500,6.4},{3200,7.2},
  {3600,7.45},{3800,7.45},{3900,7.45}
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

  // Persistent latched state (prevents transient NAN poisoning downstream)
  struct FuelLatchedState {
    float rpm     = NAN; uint32_t rpm_ms     = 0;
    float stw_kts = NAN; uint32_t stw_ms     = 0;
    float sog_kts = NAN; uint32_t sog_ms     = 0;
    float aws_kts = NAN; uint32_t aws_ms     = 0;
    float awa     = NAN; uint32_t awa_ms     = 0;
  };
  static auto* state = new FuelLatchedState();

  // Config UI
  if (!use_stw_cfg) {
    use_stw_cfg = new Linear(1.0f, 0.0f, "/config/engine_fuel/use_stw");
    ConfigItem(use_stw_cfg)
      ->set_title("Use STW for fuel model")
      ->set_description(">= 0.5 = use STW, < 0.5 = ignore STW");
  }

  // rev/s → RPM (latched to ride through transient NAN gaps)
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      if (std::isnan(rps)) return NAN;
      float r = rps * 60.0f;
      return (r < 0.0f || r > 4500.0f) ? NAN : r;
    })
  )->connect_to(
    new LambdaTransform<float,float>([=](float r){
      float v = latch_with_hold(r, state->rpm, state->rpm_ms, true);
      return (v < 0.0f) ? 0.0f : v;
    })
  );

  // Optional inputs (latched to avoid short NAN gaps)
  ValueProducer<float>* stw_kts_vp = nullptr;
  ValueProducer<float>* sog_kts_vp = nullptr;
  ValueProducer<float>* aws_kts_vp = nullptr;
  ValueProducer<float>* awa_rad_vp = nullptr;

  if (stw_ms) {
    stw_kts_vp = stw_ms->connect_to(
      new LambdaTransform<float,float>([=](float ms){
        float kts = std::isnan(ms) ? NAN : (ms * MS_TO_KTS);
        return latch_with_hold(kts, state->stw_kts, state->stw_ms);
      })
    );
  }

  if (sog_ms) {
    sog_kts_vp = sog_ms->connect_to(
      new LambdaTransform<float,float>([=](float ms){
        float kts = std::isnan(ms) ? NAN : (ms * MS_TO_KTS);
        return latch_with_hold(kts, state->sog_kts, state->sog_ms);
      })
    );
  }

  if (aws_ms) {
    aws_kts_vp = aws_ms->connect_to(
      new LambdaTransform<float,float>([=](float ms){
        float kts = std::isnan(ms) ? NAN : (ms * MS_TO_KTS);
        return latch_with_hold(kts, state->aws_kts, state->aws_ms);
      })
    );
  }

  if (awa_rad) {
    awa_rad_vp = awa_rad->connect_to(
      new LambdaTransform<float,float>([=](float rad){
        return latch_with_hold(rad, state->awa, state->awa_ms);
      })
    );
  }

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

      float stw_kts = stw_kts_vp ? stw_kts_vp->get() : NAN;
      float sog_kts = sog_kts_vp ? sog_kts_vp->get() : NAN;

      bool use_stw = (use_stw_cfg->get() >= 0.5f);
      bool stw_valid = !std::isnan(stw_kts);
      bool sog_valid = !std::isnan(sog_kts);

      bool vessel_docked = false;
      if (use_stw) {
        if ((stw_valid && stw_kts <= DOCK_SPEED_KTS) ||
            (sog_valid && sog_kts <= DOCK_SPEED_KTS)) {
          vessel_docked = true;
        }
      } else {
        if (sog_valid && sog_kts <= DOCK_SPEED_KTS) {
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
      if (aws_kts_vp && awa_rad_vp) {
        wind_factor = wind_load_factor(
          aws_kts_vp->get(),
          awa_rad_vp->get()
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
