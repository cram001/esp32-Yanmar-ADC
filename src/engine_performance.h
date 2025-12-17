#pragma once

#include <cmath>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>

using namespace sensesp;

// ============================================================================
// ENGINE PERFORMANCE MODULE
//
// Computes:
//   • Fuel flow (L/hr internally → m³/s for Signal K)
//   • Engine load (% of RATED at current RPM)
//
// Uses:
//   • User baseline table (RPM, STW, Fuel)
//   • OEM rated fuel curve (hard ceiling)
//
// Inputs:
//   • RPM
//   • STW (preferred)
//   • SOG (fallback)
//   • Apparent wind speed & angle (optional)
//
// Wind handling:
//   • Apply correction ONLY if BOTH AWS and AWA are valid
//   • Otherwise ignore wind
//
// Speed handling:
//   • Use STW if sane
//   • Else SOG
//   • Else fall back to baseline
// ============================================================================


// ============================================================================
// USER BASELINE DATA — SINGLE SOURCE OF TRUTH
// RPM, STW (knots), Fuel (L/hr)
// ============================================================================
static std::set<CurveInterpolator::Sample> baseline_stw_curve = {
  {500,  0.0}, {1000, 0.0}, {1800, 2.5}, {2000, 4.3},
  {2400, 5.0}, {2800, 6.4}, {3200, 7.3},
  {3600, 7.45}, {3800, 7.6}, {3900, 7.6}
};

static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {500,  0.6}, {1000, 0.75}, {1800, 1.95}, {2000, 2.3},
  {2400, 2.45}, {2800, 2.7}, {3200, 3.4},
  {3600, 5.0}, {3800, 6.4}, {3900, 6.5}
};


// ============================================================================
// OEM RATED FUEL CURVE — HARD CEILING
// ============================================================================
static std::set<CurveInterpolator::Sample> rated_fuel_curve = {
  {1800, 1.2}, {2000, 1.7}, {2400, 2.45},
  {2800, 3.8}, {3200, 5.25}, {3600, 7.8}, {3800, 9.5}
};


// ============================================================================
// STW FOULING RULES
// ============================================================================
static inline bool stw_invalid(float rpm, float stw) {
  if (rpm > 3000 && stw < 4.0) return true;
  if (rpm > 2500 && stw < 3.5) return true;
  if (rpm > 1000 && stw < 1.0) return true;
  return false;
}


// ============================================================================
// WIND SPEED DERATING (EMPIRICAL, USER-SUPPLIED)
// Returns fractional speed change (negative = speed loss)
// ============================================================================
static inline float wind_speed_factor(float aws_kn, float awa_rad) {
  if (std::isnan(aws_kn) || std::isnan(awa_rad)) return 0.0f;

  float awa = std::fabs(awa_rad);           // radians
  float cos_a = std::cos(awa);
  float sin_a = std::sin(awa);

  float aws = std::min(aws_kn, 30.0f);

  float head = 0.0f;
  float cross = 0.0f;

  // Head / tail wind component
  if (aws > 6.0f) {
    if (aws <= 15.0f)
      head = (aws - 6.0f) / 9.0f * 0.13f;
    else if (aws <= 20.0f)
      head = 0.13f + (aws - 15.0f) / 5.0f * 0.07f;
    else
      head = 0.20f + (aws - 20.0f) / 10.0f * 0.15f;
  }

  // Crosswind component
  if (aws > 6.0f) {
    if (aws <= 15.0f)
      cross = (aws - 6.0f) / 9.0f * 0.08f;
    else if (aws <= 20.0f)
      cross = 0.08f + (aws - 15.0f) / 5.0f * 0.02f;
    else
      cross = 0.10f + (aws - 20.0f) / 10.0f * 0.05f;
  }

  float longitudinal = -head * cos_a;   // headwind reduces speed
  float lateral = -cross * sin_a;

  return longitudinal + lateral;
}


// ============================================================================
// SETUP
// ============================================================================
inline void setup_engine_performance(
  Frequency* rpm_source,
  Transform<float, float>* stw_knots,
  Transform<float, float>* sog_knots,
  Transform<float, float>* aws_knots,
  Transform<float, float>* awa_rad
) {
  auto* rpm = rpm_source->connect_to(
    new LambdaTransform<float, float>([](float rps) {
      return rps * 60.0f;
    })
  );

  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);

  auto* actual_speed = rpm->connect_to(
    new LambdaTransform<float, float>([=](float r) {

      float stw = stw_knots ? stw_knots->get() : NAN;
      if (!std::isnan(stw) && !stw_invalid(r, stw)) {
        return stw;
      }

      float sog = sog_knots ? sog_knots->get() : NAN;
      if (!std::isnan(sog) && sog > 0.0f) {
        return sog;
      }

      return NAN;
    })
  );

  auto* fuel_lph = actual_speed->connect_to(
    new LambdaTransform<float, float>([=](float spd) {

      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f)
        return NAN;

      float expectedSTW = baseSTW;

      if (aws_knots && awa_rad) {
        float aws = aws_knots->get();
        float awa = awa_rad->get();
        if (!std::isnan(aws) && !std::isnan(awa)) {
          expectedSTW *= (1.0f + wind_speed_factor(aws, awa));
        }
      }

      float fuel;
      if (std::isnan(spd) || spd <= 0.0f || expectedSTW <= 0.0f) {
        fuel = baseFuel;   // fallback
      } else {
        fuel = baseFuel * (expectedSTW / spd);
      }

      if (!std::isnan(fuelMax))
        fuel = std::min(fuel, fuelMax);

      return fuel;
    })
  );

  auto* fuel_m3s = fuel_lph->connect_to(
    new LambdaTransform<float, float>([](float lph) {
      return std::isnan(lph) ? NAN : (lph / 1000.0f) / 3600.0f;
    })
  );

  auto* engine_load = fuel_lph->connect_to(
    new LambdaTransform<float, float>([=](float lph) {
      float fmax = rated_fuel->get();
      if (std::isnan(lph) || std::isnan(fmax) || fmax <= 0.0f)
        return NAN;
      return lph / fmax;
    })
  );

  fuel_m3s->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  engine_load->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
