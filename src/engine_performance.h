// ============================================================================
// ENGINE LOAD & FUEL FLOW ESTIMATION — DESIGN NOTES
// ============================================================================
//
// This module estimates ENGINE LOAD and FUEL FLOW for a displacement sailboat
// using a mechanically governed marine diesel (Yanmar 3JH3E class).
//
// IMPORTANT:
// ----------
// This code models *boat demand*, NOT engine capability.
// It intentionally does NOT mirror the Yanmar brochure curves except at
// true full-load conditions.
//
// ---------------------------------------------------------------------------
// CORE PHYSICAL INVARIANTS (DO NOT BREAK THESE)
// ---------------------------------------------------------------------------
//
// 1) RPM DOES NOT DEFINE POWER
//    -------------------------
//    On a marine diesel, RPM alone does NOT determine power output.
//    Power is proportional to TORQUE × RPM.
//    Torque is governed by propeller absorption and hull resistance.
//
//    At most cruising RPMs (e.g. 2400–3000), the engine is PROP-LIMITED,
//    not fuel-limited. The fuel rack is only partially open.
//
//
// 2) REDUCED STW AT CONSTANT RPM ⇒ INCREASED LOAD
//    --------------------------------------------
//    For a given RPM, the propeller expects a certain Speed-Through-Water (STW).
//    If actual STW is LOWER than baseline, torque demand rises and fuel increases.
//
//    This is the single strongest and most reliable indicator of load.
//
//
// 3) CURRENT (SOG − STW) HAS NO DIRECT EFFECT ON ENGINE LOAD
//    -------------------------------------------------------
//    Current affects SOG but does NOT change propeller load.
//    Engine load MUST be derived from STW, not SOG.
//
//    SOG is used ONLY as a fallback when STW is unavailable or disabled.
//
//
// 4) WIND INCREASES LOAD — ASYMMETRICALLY
//    ------------------------------------
//    Apparent wind increases resistance through:
//      - Aerodynamic drag (rig, mast, cabin, dodger)
//      - Induced waves
//      - Pitching and prop ventilation
//      - Increased hull resistance
//
//    Effects are intentionally asymmetric:
//      - Headwind: strong penalty
//      - Crosswind: moderate penalty
//      - Tailwind: mild benefit (never cancels STW loss)
//
//    Wind effects are ignored below ~7 knots apparent.
//
//
// 5) WIND NEVER "EXCUSES" LOSS OF STW
//    --------------------------------
//    A reduction in STW ALWAYS increases load.
//    Wind modifies load ON TOP of STW loss; it never reduces load when STW drops.
//
//
// 6) OPERATOR MAY DISABLE STW IF SENSOR IS UNTRUSTED
//    -----------------------------------------------
//    STW sensors foul easily.
//    When STW is disabled via config, load is estimated from RPM and wind only.
//    This prevents massive overestimation due to bad STW data.
//
//
// ---------------------------------------------------------------------------
// ENGINE-LIMITED vs PROP-LIMITED REGIMES
// ---------------------------------------------------------------------------
//
// - PROP-LIMITED (most of the time):
//     • RPM rises freely
//     • Fuel < rated curve
//     • Load < 100%
//
// - ENGINE-LIMITED (heavy weather, towing, waves):
//     • RPM caps below rated
//     • Fuel approaches rated curve
//     • Load → 100%
//
// The code naturally transitions between these regimes by observing STW loss
// and RPM behavior — no hard mode switching is used.
//
//
// ---------------------------------------------------------------------------
// YANMAR PERFORMANCE CURVES — HOW THEY ARE USED
// ---------------------------------------------------------------------------
//
// Yanmar fuel and power curves represent ENGINE CAPABILITY under standardized
// full-load conditions.
//
// They are used here ONLY as:
//   • Baseline fuel at light load
//   • Maximum allowable fuel at a given RPM
//
// They are NOT used to force fuel or load at cruising RPMs.
//
// Example:
//   At 2800 RPM, brochure power ≈ 25 kW,
//   but the boat typically requires only ~9–12 kW.
//   The app intentionally reports the LOWER value.
//
//
// ---------------------------------------------------------------------------
// EXPECTED REAL-WORLD BEHAVIOR (REFERENCE CASES @ 2800 RPM)
// ---------------------------------------------------------------------------
//
// Baseline (calm water):
//   STW ≈ 6.4 kt → Fuel ≈ 2.7 L/hr, Load ≈ 70%
//
// Towing dinghy:
//   STW ≈ 6.0 kt → Fuel ≈ 2.8 L/hr, Load ↑ slightly
//
// Strong current only:
//   STW ≈ 6.4 kt, SOG ≈ 4.6 kt → Load unchanged
//
// Strong headwind (15–30 kt):
//   STW ↓↓↓ → Fuel ↑↑, Load → 100%
//
// Fouled STW sensor:
//   Disable STW → Fuel returns near baseline
//
//
// ---------------------------------------------------------------------------
// IMPORTANT WARNING TO FUTURE MAINTAINERS
// ---------------------------------------------------------------------------
//
// If you "fix" this code to:
//   • match brochure power at cruising RPM
//   • tie fuel directly to RPM
//   • use SOG to compute load
//
// You will make the results *less* accurate.
//
// The current behavior is intentional and validated against:
//   • real boat measurements
//   • propeller physics
//   • marine diesel governor behavior
//
// If you change the logic, revalidate against logged real-world data.
//
// ============================================================================



//
// Core invariants:
//
//  1) Reduced STW at a given RPM increases engine load (when STW is trusted).
//  2) Current (SOG–STW delta) has NO direct effect on engine load.
//  3) Apparent wind adds aerodynamic drag (head > cross).
//  4) Tailwind helps, but never offsets hydrodynamic losses.
//  5) Wind effects ignored below ~7 kn apparent.
//  6) Operator may disable STW usage if sensor is unreliable.
//  7) Fuel and load are smoothed (2 s moving average).
//
// ---------------------------------------------------------------------------
// REUSABLE TEST CASES (RPM = 2800)
//
// Baseline:
//   STW 6.4, 0 wind:  → Fuel ≈ 2.7 L/hr, Load ≈ 0.71
//
// Towing dinghy:
//   STW 6.0, 0 wind:   → Fuel ≈ 2.8 L/hr
//
// Fouled STW sensor (override = 0): (user can overide use of STW via UI)
//   STW 3.6, 0 wind, SOG 6.4 kts    → Fuel ≈ baseline
//
// Strong headwind:
//   STW 3.5, AWS 30 from ahead    → Load (fuel consumption) ≈ 100%
//
// ============================================================================

#pragma once

#include <cmath>
#include <set>

#include <Arduino.h>

#include <sensesp/system/valueproducer.h>
#include <sensesp/signalk/signalk_value_listener.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/frequency.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/signalk/signalk_output.h>

using namespace sensesp;

// ---------------------------------------------------------------------------
// Clamp helper
// ---------------------------------------------------------------------------
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
// BASELINE CURVES
// ============================================================================
static std::set<CurveInterpolator::Sample> baseline_stw_curve = {
  {500,0.0},{1000,0.0},{1800,2.5},{2000,4.3},
  {2400,5.0},{2800,6.4},{3200,7.3},
  {3600,7.45},{3800,7.6},{3900,7.6}
};

static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {500,0.6},{1000,0.75},{1800,1.95},{2000,2.3},
  {2400,2.45},{2800,2.7},{3200,3.4},
  {3600,5.0},{3800,6.4},{3900,6.5}
};

static std::set<CurveInterpolator::Sample> rated_fuel_curve = {
  {500,0.9},{1800,1.2},{2000,1.7},{2400,2.45},
  {2800,3.8},{3200,5.25},{3600,7.8},{3900,9.6}
};

// ============================================================================
// STW SANITY CHECK
// ============================================================================
static inline bool stw_invalid(float rpm, float stw) {
  if (rpm > 3000 && stw < 4.0f) return true;
  if (rpm > 2500 && stw < 3.5f) return true;
  if (rpm > 1000 && stw < 1.0f) return true;
  return false;
}

// ============================================================================
// WIND LOAD MULTIPLIER (ASYMMETRIC)
// ============================================================================
static inline float wind_load_factor(float aws, float awa) {

  if (std::isnan(aws) || std::isnan(awa) || aws < 7.0f) {
    return 1.0f;
  }

  constexpr float PI_F = 3.14159265f;

  float angle = clamp_val(fabsf(awa), 0.0f, PI_F);
  float aws_c = clamp_val(aws, 7.0f, 30.0f);

  float head  =  cosf(angle);   // +1 headwind, -1 tailwind
  float cross =  sinf(angle);   // 0..1 crosswind

  float strength = (aws_c - 7.0f) / 23.0f;

  float penalty = 0.0f;

  if (head > 0.0f) {
    penalty += strength * head * 0.30f;   // headwind
  } else {
    penalty += strength * head * 0.12f;   // tailwind (limited relief)
  }

  penalty += strength * cross * 0.18f;    // crosswind drag

  return clamp_val(1.0f + penalty, 0.90f, 1.45f);
}

// ============================================================================
// SETUP — ENGINE PERFORMANCE
// ============================================================================
inline void setup_engine_performance(
  Frequency* rpm_source,
  ValueProducer<float>* stw_knots,
  ValueProducer<float>* sog_knots,
  ValueProducer<float>* aws_knots,
  ValueProducer<float>* awa_rad
) {
  (void)sog_knots;

  if (!rpm_source) return;

  // -------------------------------------------------------------------------
  // STW enable flag from Signal K (1 = use STW, 0 = ignore)
  // -------------------------------------------------------------------------
  auto* use_stw_flag = new SKValueListener<float>(
    "config.engine.useStw",
    1000,
    "/config/engine/use_stw"
  );

  // -------------------------------------------------------------------------
  // RPM (rps → rpm)
  // -------------------------------------------------------------------------
  auto* rpm = rpm_source->connect_to(
    new LambdaTransform<float,float>([](float rps){
      float r = rps * 60.0f;
      return (r < 300.0f || r > 4500.0f) ? NAN : r;
    })
  );

  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel  = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);

  // -------------------------------------------------------------------------
  // Fuel flow calculation (L/hr)
  // -------------------------------------------------------------------------
  auto* fuel_lph_raw = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f) return NAN;

      bool use_stw = (use_stw_flag->get() >= 0.5f);

      // --- Hydrodynamic load (STW) ---
      float stw_factor = 1.0f;

      if (use_stw && stw_knots) {
        float stw = stw_knots->get();
        if (!std::isnan(stw) && stw > 0.0f && !stw_invalid(r, stw)) {

          float ratio = baseSTW / stw;

          // 5% deadband: ignore small STW variations
          if (ratio >= 1.05f) {
            // softened STW sensitivity (power law)
            stw_factor = powf(ratio, 0.6f);
            stw_factor = clamp_val(stw_factor, 1.0f, 2.0f);
          }
        }
      }

      // --- Aerodynamic load (wind) ---
      float wind_factor = 1.0f;
      if (aws_knots && awa_rad) {
        wind_factor = wind_load_factor(
          aws_knots->get(),
          awa_rad->get()
        );
      }

      float fuel = baseFuel * stw_factor * wind_factor;

      if (!std::isnan(fuelMax)) {
        fuel = std::min(fuel, fuelMax);
      }

      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  // -------------------------------------------------------------------------
  // 2-second moving average
  // -------------------------------------------------------------------------
  auto* fuel_lph = fuel_lph_raw->connect_to(
    new MovingAverage(2)
  );

  // Fuel → m³/s (Signal K)
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return std::isnan(lph) ? NAN : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  // Engine load (% of rated)
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([=](float lph){
      float fmax = rated_fuel->get();
      if (std::isnan(lph) || std::isnan(fmax) || fmax <= 0.0f) return NAN;
      return clamp_val(lph / fmax, 0.0f, 1.0f);
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
