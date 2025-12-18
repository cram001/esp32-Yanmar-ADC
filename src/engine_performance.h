// ============================================================================
// ENGINE PERFORMANCE & FUEL FLOW ESTIMATION  (SensESP v3.1.0 compatible)
// ============================================================================
//
// Core invariants:
//  1) Reduced STW at a given RPM increases load (when STW is trusted).
//  2) Current (SOG–STW delta) has NO direct effect on engine load.
//  3) Apparent wind adds aerodynamic drag (head & crosswind ↑ load).
//  4) Wind NEVER excuses a loss of STW.
//  5) Wind effects ignored below ~7 kn apparent.
//  6) Operator may disable STW usage via Signal K config (1 = use, 0 = ignore).
//
// ---------------------------------------------------------------------------
// REUSABLE TEST CASES (RPM = 2800)
//
// Baseline:
//   STW 6.4, calm → Fuel ≈ 2.7 L/hr, Load ≈ 71%
//
// Towing:
//   STW 5.9 → Fuel ↑, Load ↑
//
// Fouled STW sensor (override = 0):
//   STW 3.6 → Fuel ≈ baseline
//
// Strong current only:
//   STW 6.4, SOG 4.6 → Load unchanged
//
// Strong headwind:
//   STW 5.2, AWS 15 → Fuel ↑↑
//
// Strong crosswind:
//   STW 4.9, AWS 25 @ 75° → Fuel ↑↑
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
// STW sanity
// ============================================================================
static inline bool stw_invalid(float rpm, float stw) {
  if (rpm > 3000 && stw < 4.0f) return true;
  if (rpm > 2500 && stw < 3.5f) return true;
  if (rpm > 1000 && stw < 1.0f) return true;
  return false;
}

// ============================================================================
// Wind load multiplier
// ============================================================================
static inline float wind_load_factor(float aws, float awa) {
  if (std::isnan(aws) || std::isnan(awa) || aws < 7.0f) return 1.0f;

  constexpr float PI_F = 3.14159265f;
  float a = clamp_val(fabsf(awa), 0.0f, PI_F);

  float head  = cosf(a);
  float cross = sinf(a);

  aws = clamp_val(aws, 7.0f, 30.0f);

  float factor =
    1.0f + (aws - 7.0f) / 23.0f * (0.25f * head + 0.20f * cross);

  return clamp_val(factor, 0.9f, 1.4f);
}

// ============================================================================
// SETUP
// ============================================================================
inline void setup_engine_performance(
  Frequency* rpm_source,
  ValueProducer<float>* stw_knots,
  ValueProducer<float>* sog_knots,
  ValueProducer<float>* aws_knots,
  ValueProducer<float>* awa_rad
) {
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
      return (r < 300 || r > 4500) ? NAN : r;
    })
  );

  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel  = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);

  auto* fuel_lph = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel)) return NAN;

      bool use_stw = (use_stw_flag->get() >= 0.5f);

      float stw_factor = 1.0f;
      if (use_stw && stw_knots) {
        float stw = stw_knots->get();
        if (!std::isnan(stw) && stw > 0 && !stw_invalid(r, stw)) {
          stw_factor = clamp_val(baseSTW / stw, 1.0f, 2.5f);
        }
      }

      float wind_factor = 1.0f;
      if (aws_knots && awa_rad) {
        wind_factor = wind_load_factor(
          aws_knots->get(), awa_rad->get()
        );
      }

      float fuel = baseFuel * stw_factor * wind_factor;
      if (!std::isnan(fuelMax)) fuel = std::min(fuel, fuelMax);

      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return std::isnan(lph) ? NAN : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  fuel_lph->connect_to(
    new LambdaTransform<float,float>([=](float lph){
      float fmax = rated_fuel->get();
      if (std::isnan(lph) || std::isnan(fmax)) return NAN;
      return clamp_val(lph / fmax, 0.0f, 1.0f);
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
