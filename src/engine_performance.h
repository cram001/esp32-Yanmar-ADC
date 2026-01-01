#pragma once

/*
 * ============================================================================
 * ENGINE PERFORMANCE & FUEL FLOW ESTIMATION
 * ============================================================================
 *
 * PURPOSE
 * -------
 * Estimates:
 *   • Fuel flow
 *   • Engine load
 *
 * DESIGN CONTRACT (CRITICAL)
 * --------------------------
 *  • Input engine speed MUST be in revolutions per second (rev/s)
 *  • Input MUST already be smoothed and free of transient 0 / NAN
 *  • This module MUST NOT be fed directly from a raw Frequency transform
 *
 * Canonical source:
 *   rpm_sensor.h → g_engine_rev_s_smooth
 *
 * UNIT CONVENTIONS
 * ----------------
 *  • Fuel flow internal: liters/hour (L/h)
 *  • Fuel flow output:   m³/s (Signal K requirement)
 *  • Engine load:        0.0–1.0 (fraction of rated)
 *
 * OPERATIONAL MODES
 * -----------------
 *  1) UNDERWAY
 *     (STW ≥ 0.15 kt AND SOG ≥ 0.15 kt, subject to STW enable)
 *     → Fuel is load-coupled using:
 *         - RPM baseline fuel curve
 *         - STW deficit penalty
 *         - Wind drag penalty
 *
 *  2) STATIONARY / DOCK
 *     (STW < 0.15 kt OR SOG < 0.15 kt, respecting STW disable)
 *     → Fuel derived ONLY from an RPM-indexed idle curve
 *     → No STW or wind effects applied
 *
 * SPECIAL CASE
 * ------------
 *  • If BOTH STW and SOG are NAN:
 *      → Vessel state is unknown
 *      → Fall back to baseline fuel curve (RPM-only)
 *
 * ENGINE LOAD RULE
 * ----------------
 *  • When engine is running and vessel is stationary,
 *    engine load is clamped to a minimum of 8 %
 *
 * NON-GOALS
 * ---------
 *  • No gearbox awareness (neutral vs in-gear)
 *  • No alternator current → torque modelling
 *  • No RPM hysteresis or debounce
 * ============================================================================
 */

#include <Arduino.h>
#include <cmath>
#include <set>

#include <sensesp/system/valueproducer.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/transforms/linear.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/ui/config_item.h>

using namespace sensesp;

// ============================================================================
// CONSTANTS
// ============================================================================

// Centralized dock / stationary threshold
constexpr float DOCK_SPEED_KTS = 0.15f;

// ============================================================================
// HELPERS
// ============================================================================

// Arduino-safe clamp (avoids std::min/max macro collisions)
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Max output helper for curve sets (used for load normalization)
static inline float max_curve_output(const std::set<CurveInterpolator::Sample>& curve) {
  float max_val = NAN;
  for (const auto& sample : curve) {
    if (std::isnan(max_val) || sample.output_ > max_val) {
      max_val = sample.output_;
    }
  }
  return max_val;
}

// ============================================================================
// CONFIG CARRIER — USE STW FLAG
// ============================================================================
//
// Linear is used ONLY as a configuration carrier here.
// Value >= 0.5 → STW enabled
// Value <  0.5 → STW ignored
//
static Linear* use_stw_cfg = nullptr;

// ============================================================================
// BASELINE CURVES (RPM → STW / Fuel)
// ============================================================================

static std::set<CurveInterpolator::Sample> baseline_stw_curve = {
  {500,0.0},{1000,0.0},{1800,2.5},{2000,4.3},
  {2400,5.0},{2800,6.4},{3200,7.3},
  {3600,7.45},{3800,7.6},{3900,7.6}
};

static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {500,0.6},{1000,0.75},{1800,1.1},{2000,1.5},
  {2400,2.0},{2800,2.7},{3200,3.4},
  {3600,5.0},{3800,6.4},{3900,6.5}
};

static std::set<CurveInterpolator::Sample> rated_fuel_curve = {
  {500,0.9},{1800,1.4},{2000,1.8},{2400,2.45},
  {2800,3.8},{3200,5.25},{3600,7.8},{3900,9.6}
};

static const float rated_fuel_curve_peak = max_curve_output(rated_fuel_curve);

// ============================================================================
// IDLE / DOCK FUEL CURVE (RPM → L/h)
// ============================================================================
//
// Used ONLY when vessel is classified as stationary
//
static std::set<CurveInterpolator::Sample> idle_fuel_curve = {
  { 800,  0.6 },
  { 3600, 1.4 }
};

// ============================================================================
// STW SANITY CHECK (detects bad sensors / fouling)
// ============================================================================
static inline bool stw_invalid(float rpm, float stw) {
  if (rpm > 3000 && stw < 4.0f) return true;
  if (rpm > 2500 && stw < 3.5f) return true;
  if (rpm > 1000 && stw < 1.0f) return true;
  return false;
}

// ============================================================================
// WIND LOAD MULTIPLIER
// ============================================================================
static inline float wind_load_factor(float aws, float awa) {

  if (std::isnan(aws) || std::isnan(awa) || aws < 7.0f) {
    return 1.0f;
  }

  constexpr float PI_F = 3.14159265f;

  float angle = clamp_val(fabsf(awa), 0.0f, PI_F);
  float aws_c = clamp_val(aws, 7.0f, 30.0f);

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
// SETUP — ENGINE PERFORMANCE PIPELINE
// ============================================================================
inline void setup_engine_performance(
  ValueProducer<float>* rpm_rev_s,   // canonical engine speed (rev/s)
  ValueProducer<float>* stw_knots,
  ValueProducer<float>* sog_knots,
  ValueProducer<float>* aws_knots,
  ValueProducer<float>* awa_rad
) {
  if (!rpm_rev_s) return;

  // -------------------------------------------------------------------------
  // CONFIGURATION UI (REGISTER ONCE)
  // -------------------------------------------------------------------------
  static bool cfg_registered = false;
  if (!cfg_registered) {

    use_stw_cfg = new Linear(1.0f, 0.0f, "/config/engine/use_stw");

    ConfigItem(use_stw_cfg)
      ->set_title("Use STW for engine performance")
      ->set_description(">= 0.5 = use STW, < 0.5 = ignore STW")
      ->set_sort_order(90);

    cfg_registered = true;
  }

  // -------------------------------------------------------------------------
  // rev/s → RPM (engine-performance domain)
  // -------------------------------------------------------------------------
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      float r = rps * 60.0f;
      return (r < 300.0f || r > 4500.0f) ? NAN : r;
    })
  );

  // Feed constant into config carrier
  rpm->connect_to(new LambdaTransform<float,float>([](float){ return 1.0f; }))
     ->connect_to(use_stw_cfg);

  // -------------------------------------------------------------------------
  // Curve interpolators
  // -------------------------------------------------------------------------
  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel  = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);
  auto* idle_fuel  = new CurveInterpolator(&idle_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);
  rpm->connect_to(idle_fuel);

  // -------------------------------------------------------------------------
  // FUEL FLOW CALCULATION (L/h)
  // -------------------------------------------------------------------------
  auto* fuel_lph_raw = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      if (std::isnan(r)) return NAN;

      float stw = stw_knots ? stw_knots->get() : NAN;
      float sog = sog_knots ? sog_knots->get() : NAN;

      bool use_stw = (use_stw_cfg->get() >= 0.5f);

      bool stw_valid = !std::isnan(stw);
      bool sog_valid = !std::isnan(sog);

      // ---------------------------------------------------------------------
      // STW usability for physics (data quality, not vessel state)
      // ---------------------------------------------------------------------
      bool stw_usable_for_physics =
          use_stw &&
          stw_valid &&
          stw >= DOCK_SPEED_KTS &&
          !stw_invalid(r, stw);

      // ---------------------------------------------------------------------
      // Vessel docked state (operational classification)
      // ---------------------------------------------------------------------
      bool vessel_docked = false;

      if (use_stw) {
        if ((stw_valid && stw < DOCK_SPEED_KTS) ||
            (sog_valid && sog < DOCK_SPEED_KTS)) {
          vessel_docked = true;
        }
      } else {
        if (sog_valid && sog < DOCK_SPEED_KTS) {
          vessel_docked = true;
        }
      }
      // If both speeds NAN → unknown → NOT docked

      // ---------------------------------------------------------------------
      // DOCK / STATIONARY MODE → RPM-indexed idle fuel
      // ---------------------------------------------------------------------
      if (vessel_docked) {
        float idle = idle_fuel->get();
        float fmax = rated_fuel->get();

        if (!std::isnan(idle)) {
          if (!std::isnan(fmax) && idle > fmax) idle = fmax;
          return clamp_val(idle, 0.0f, 14.0f);
        }
      }

      // ---------------------------------------------------------------------
      // UNDERWAY / UNKNOWN MODE → load-coupled or baseline fuel
      // ---------------------------------------------------------------------
      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f) return NAN;

      float stw_factor = 1.0f;

      if (stw_usable_for_physics && baseSTW > DOCK_SPEED_KTS) {
        float ratio = baseSTW / stw;
        if (ratio >= 1.05f) {
          stw_factor = clamp_val(powf(ratio, 0.6f), 1.0f, 2.0f);
        }
      }

      float wind_factor = 1.0f;
      if (aws_knots && awa_rad) {
        wind_factor = wind_load_factor(
          aws_knots->get(),
          awa_rad->get()
        );
      }

      float fuel = baseFuel * stw_factor * wind_factor;
      if (!std::isnan(fuelMax) && fuel > fuelMax) fuel = fuelMax;

      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  auto* fuel_lph = fuel_lph_raw->connect_to(new MovingAverage(2));

  // -------------------------------------------------------------------------
  // OUTPUT — Fuel rate (m³/s) for Signal K
  // -------------------------------------------------------------------------
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return std::isnan(lph) ? NAN : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  // -------------------------------------------------------------------------
  // OUTPUT — Engine load for Signal K
  // -------------------------------------------------------------------------
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([=](float lph){

      const float fmax = rated_fuel_curve_peak;  // normalize against full-load peak
      if (std::isnan(lph) || std::isnan(fmax) || fmax <= 0.0f) return NAN;

      float load = lph / fmax;

      float stw = stw_knots ? stw_knots->get() : NAN;
      float sog = sog_knots ? sog_knots->get() : NAN;

      bool use_stw = (use_stw_cfg->get() >= 0.5f);

      bool stw_valid = !std::isnan(stw);
      bool sog_valid = !std::isnan(sog);

      bool vessel_docked = false;
      if (use_stw) {
        if ((stw_valid && stw < DOCK_SPEED_KTS) ||
            (sog_valid && sog < DOCK_SPEED_KTS)) {
          vessel_docked = true;
        }
      } else {
        if (sog_valid && sog < DOCK_SPEED_KTS) {
          vessel_docked = true;
        }
      }

      // Minimum dock load clamp (engine running, no propulsive work)
      if (load > 0.0f && vessel_docked) {
        if (load < 0.08f) load = 0.08f;
      }

      return clamp_val(load, 0.0f, 1.0f);
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
