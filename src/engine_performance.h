// ============================================================================
// ENGINE PERFORMANCE & FUEL FLOW ESTIMATION
// ============================================================================
//
// Design invariants:
//  1) At a given RPM, reduced STW MUST increase engine load.
//  2) Current (SOG–STW delta) has no direct effect on engine load.
//  3) Apparent wind represents aerodynamic drag:
//     • Headwind  → increases load
//     • Crosswind → increases load (leeway + rudder drag)
//     • Tailwind  → may slightly reduce load (bounded)
//  4) Wind must NEVER excuse a loss of STW.
//  5) Wind effects are ignored below ~7 knots apparent.
//
// ---------------------------------------------------------------------------
// REUSABLE TEST CASES (RPM = 2800)
//
// Baseline:
//   STW 6.4, calm → Fuel ≈ 2.7 L/hr, Load ≈ 71%
//
// Case 1 (towing):
//   STW 5.9, AWS 5 → Fuel ↑ (~2.9), Load ↑
//
// Case 2 (favorable current only):
//   STW 6.4, SOG 4.6 → Fuel = baseline, Load = baseline
//
// Case 3 (towing + headwind):
//   STW 5.2, AWS 15 head → Fuel ↑↑
//
// Case 4 (strong current + headwind):
//   STW 5.9, SOG 7.5, AWS 15 head → Fuel ↑
//
// Case 7 (strong crosswind):
//   STW 4.9, AWS 25 @ 75° → Fuel ↑↑
//
// ============================================================================

#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <set>

#include <Arduino.h>

#include <sensesp/system/valueproducer.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/transforms/frequency.h>

using namespace sensesp;

// ---------------------------------------------------------------------------
// Debug guard
// ---------------------------------------------------------------------------
#ifndef ENABLE_DEBUG_OUTPUTS
#define ENABLE_DEBUG_OUTPUTS 0
#endif

// ---------------------------------------------------------------------------
// Clamp helper (C++11 safe)
// ---------------------------------------------------------------------------
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
// BASELINE CURVES (RPM → STW, Fuel)
// ============================================================================
static std::set<CurveInterpolator::Sample> baseline_stw_curve = {
  {500, 0.0}, {1000, 0.0}, {1800, 2.5}, {2000, 4.3},
  {2400, 5.0}, {2800, 6.4}, {3200, 7.3},
  {3600, 7.45}, {3800, 7.6}, {3900, 7.6}
};

static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {500, 0.6}, {1000, 0.75}, {1800, 1.95}, {2000, 2.3},
  {2400, 2.45}, {2800, 2.7}, {3200, 3.4},
  {3600, 5.0}, {3800, 6.4}, {3900, 6.5}
};

static std::set<CurveInterpolator::Sample> rated_fuel_curve = {
  {500, 0.9}, {1800, 1.2}, {2000, 1.7}, {2400, 2.45},
  {2800, 3.8}, {3200, 5.25}, {3600, 7.8}, {3900, 9.6}
};

// ============================================================================
// STW SANITY FILTER
// ============================================================================
static inline bool stw_invalid(float rpm, float stw) {
  if (rpm > 3000 && stw < 4.0f) return true;
  if (rpm > 2500 && stw < 3.5f) return true;
  if (rpm > 1000 && stw < 1.0f) return true;
  return false;
}

// ============================================================================
// WIND LOAD MULTIPLIER
// Converts apparent wind into a load modifier.
//  • < 7 kn → ignored
//  • Head & crosswind → increase load
//  • Tailwind → small bounded reduction
// ============================================================================
static inline float wind_load_factor(float aws_kn, float awa_rad) {
  if (std::isnan(aws_kn) || std::isnan(awa_rad)) return 1.0f;
  if (aws_kn < 7.0f) return 1.0f;

  const float PI = 3.14159265f;
  float awa = clamp_val(std::fabs(awa_rad), 0.0f, PI);

  float head_comp  = std::cos(awa);   // +1 headwind, -1 tailwind
  float cross_comp = std::sin(awa);   // 0..1 crosswind

  float aws = clamp_val(aws_kn, 7.0f, 30.0f);

  // Conservative linear scaling (tuned for auxiliaries)
  float head_load  = (aws - 7.0f) / 23.0f * 0.25f * head_comp;
  float cross_load = (aws - 7.0f) / 23.0f * 0.20f * cross_comp;

  float factor = 1.0f + head_load + cross_load;

  // Tailwind help is limited; wind must never erase STW penalty
  return clamp_val(factor, 0.90f, 1.40f);
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
  if (!rpm_source ||
      baseline_fuel_curve.empty() ||
      baseline_stw_curve.empty() ||
      rated_fuel_curve.empty()) {
    return;
  }

  constexpr float RPM_MIN = 300.0f;
  constexpr float RPM_MAX = 4500.0f;
  constexpr float SPEED_MAX = 25.0f;
  constexpr float FUEL_MAX_ABS = 14.0f;
  constexpr float LOAD_MAX = 1.0f;

  auto* rpm = rpm_source->connect_to(
    new LambdaTransform<float, float>([](float rps) {
      float r = rps * 60.0f;
      if (std::isnan(r) || r < RPM_MIN || r > RPM_MAX) return NAN;
      return r;
    })
  );

  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel  = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);

#if ENABLE_DEBUG_OUTPUTS
  base_fuel->connect_to(new SKOutputFloat("debug.engine.baselineFuel"));
  base_stw->connect_to(new SKOutputFloat("debug.engine.baselineSTW"));
#endif

  auto* fuel_lph = rpm->connect_to(
    new LambdaTransform<float, float>([=](float r) {

      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f) return NAN;

      float stw = NAN;
      if (stw_knots) {
        float s = stw_knots->get();
        if (!std::isnan(s) && s > 0.0f && s < SPEED_MAX &&
            !stw_invalid(r, s)) {
          stw = s;
        }
      }

      // Core invariant: STW deficit drives load
      float stw_factor =
        (!std::isnan(stw) && stw > 0.0f)
          ? clamp_val(baseSTW / stw, 1.0f, 2.5f)
          : 1.0f;

      // Wind adds load but never excuses STW loss
      float wind_factor = 1.0f;
      if (aws_knots && awa_rad) {
        wind_factor = wind_load_factor(
          aws_knots->get(), awa_rad->get()
        );
      }

      float fuel = baseFuel * stw_factor * wind_factor;

      if (!std::isnan(fuelMax))
        fuel = (fuel > fuelMax) ? fuelMax : fuel;

#if ENABLE_DEBUG_OUTPUTS
      static float dbg_stw_factor;
      static float dbg_wind_factor;
      dbg_stw_factor  = stw_factor;
      dbg_wind_factor = wind_factor;
#endif

      return clamp_val(fuel, 0.0f, FUEL_MAX_ABS);
    })
  );

#if ENABLE_DEBUG_OUTPUTS
  fuel_lph->connect_to(new SKOutputFloat("debug.engine.fuelLph"));
#endif

  fuel_lph->connect_to(
    new LambdaTransform<float, float>([](float lph) {
      return std::isnan(lph) ? NAN : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  fuel_lph->connect_to(
    new LambdaTransform<float, float>([=](float lph) {
      float fmax = rated_fuel->get();
      if (std::isnan(lph) || std::isnan(fmax) || fmax <= 0.0f) return NAN;
      return clamp_val(lph / fmax, 0.0f, LOAD_MAX);
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
