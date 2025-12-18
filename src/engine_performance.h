#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <set>

#include <Arduino.h>   // millis()

#include <sensesp/system/valueproducer.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/transforms/frequency.h>

using namespace sensesp;

// ---------------------------------------------------------------------------
// C++11-compatible clamp helper
// ---------------------------------------------------------------------------
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
// BASELINE CURVES
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

static std::set<CurveInterpolator::Sample> rated_fuel_curve = {
  {1800, 1.2}, {2000, 1.7}, {2400, 2.45},
  {2800, 3.8}, {3200, 5.25}, {3600, 7.8}, {3800, 9.5}
};

// ============================================================================
// STW FOULING RULES
// ============================================================================
static inline bool stw_invalid(float rpm, float stw) {
  if (rpm > 3000 && stw < 4.0f) return true;
  if (rpm > 2500 && stw < 3.5f) return true;
  if (rpm > 1000 && stw < 1.0f) return true;
  return false;
}

// ============================================================================
// WIND SPEED DERATING
// ============================================================================
static inline float wind_speed_factor(float aws_kn, float awa_rad) {
  if (std::isnan(aws_kn) || std::isnan(awa_rad)) return 0.0f;

  const float PI_F = 3.14159265f;
  float awa = clamp_val(std::fabs(awa_rad), 0.0f, PI_F);

  float cos_a = std::cos(awa);
  float sin_a = std::sin(awa);

  float aws = (aws_kn > 30.0f) ? 30.0f : aws_kn;

  float head = 0.0f;
  float cross = 0.0f;

  if (aws > 6.0f) {
    if (aws <= 15.0f)      head = (aws - 6.0f) / 9.0f * 0.13f;
    else if (aws <= 20.0f) head = 0.13f + (aws - 15.0f) / 5.0f * 0.07f;
    else                   head = 0.20f + (aws - 20.0f) / 10.0f * 0.15f;
  }

  if (aws > 6.0f) {
    if (aws <= 15.0f)      cross = (aws - 6.0f) / 9.0f * 0.08f;
    else if (aws <= 20.0f) cross = 0.08f + (aws - 15.0f) / 5.0f * 0.02f;
    else                   cross = 0.10f + (aws - 20.0f) / 10.0f * 0.05f;
  }

  return (-head * cos_a) - (cross * sin_a);
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
  if (baseline_fuel_curve.empty() ||
      baseline_stw_curve.empty() ||
      rated_fuel_curve.empty()) {
    return;
  }

  constexpr float RPM_MIN = 300.0f;
  constexpr float RPM_MAX = 4500.0f;

  constexpr float SPEED_MAX = 25.0f;
  constexpr float FUEL_MAX_ABS = 12.0f;
  constexpr float LOAD_MAX = 1.2f;

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

  auto* fuel_lph = rpm->connect_to(
    new LambdaTransform<float, float>([=](float r) {
      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f) return NAN;

      float spd = NAN;

      if (stw_knots) {
        float stw = stw_knots->get();
        if (!std::isnan(stw) && stw > 0.0f && stw < SPEED_MAX &&
            !stw_invalid(r, stw))
          spd = stw;
      }

      if (std::isnan(spd) && sog_knots) {
        float sog = sog_knots->get();
        if (!std::isnan(sog) && sog > 0.0f && sog < SPEED_MAX)
          spd = sog;
      }

      float expectedSTW = baseSTW;
      if (aws_knots && awa_rad) {
        float aws = aws_knots->get();
        float awa = awa_rad->get();
        if (!std::isnan(aws) && !std::isnan(awa)) {
          float wf = wind_speed_factor(aws, awa);
          wf = clamp_val(wf, -0.4f, 0.1f);
          expectedSTW *= (1.0f + wf);
        }
      }

      float fuel = (std::isnan(spd) || spd <= 0.0f || expectedSTW <= 0.0f)
                     ? baseFuel
                     : baseFuel * (expectedSTW / spd);

      if (!std::isnan(fuelMax))
        fuel = (fuel > fuelMax) ? fuelMax : fuel;

      return clamp_val(fuel, 0.0f, FUEL_MAX_ABS);
    })
  );

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
