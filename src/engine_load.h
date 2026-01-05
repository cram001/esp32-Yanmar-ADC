// engine_load.h
#pragma once

/*
 * ============================================================================
 * ENGINE LOAD — kW-BASED (AUTHORITATIVE)
 * ============================================================================
 *
 * PURPOSE
 * -------
 *  • Compute engine load from fuel burn and max power curve
 *
 * PUBLISHES
 * ---------
 *  • propulsion.engine.load   (0.0 – 1.0)
 *
 * CONTRACT
 * --------
 *  • fuel_lph must be finite (0 = engine off)
 *  • RPM must be rev/s (smoothed preferred)
 *  • Load is NEVER NAN
 * ============================================================================
 */

#include <cmath>
#include <set>

#include <sensesp/system/valueproducer.h>
#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>

using namespace sensesp;

// ============================================================================
// MAX POWER CURVE (kW) — Yanmar 3JH3E EPA
// ============================================================================
static std::set<CurveInterpolator::Sample> max_power_curve = {
  {1800, 17.9f},
  {2000, 20.9f},
  {2400, 24.6f},
  {2800, 26.8f},
  {3200, 28.3f},
  {3600, 29.5f},
  {3800, 29.83f}
};

// ============================================================================
// CONSTANTS
// ============================================================================
static constexpr float BSFC_G_PER_KWH        = 240.0f;
static constexpr float FUEL_DENSITY_KG_PER_L = 0.84f;
static constexpr float ENGINE_RUNNING_RPM   = 500.0f;

// ============================================================================
// SETUP — ENGINE LOAD
// ============================================================================
inline void setup_engine_load(
  ValueProducer<float>* rpm_rev_s,
  Transform<float,float>* fuel_lph
) {
  if (!rpm_rev_s || !fuel_lph) return;

  // rev/s → RPM
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      if (std::isnan(rps)) return NAN;
      float r = rps * 60.0f;
      return (r < 0.0f || r > 4500.0f) ? NAN : r;
    })
  );

  // Max power at RPM
  auto* max_power = new CurveInterpolator(&max_power_curve);
  rpm->connect_to(max_power);

  // Actual power from fuel (kW)
  auto* actual_power_kW =
    fuel_lph->connect_to(new LambdaTransform<float,float>(
      [](float lph){
        if (std::isnan(lph) || lph <= 0.0f) return 0.0f;
        return (lph * FUEL_DENSITY_KG_PER_L * 1000.0f) / BSFC_G_PER_KWH;
      })
    );

  // Load fraction
  actual_power_kW->connect_to(
    new LambdaTransform<float,float>([=](float kW){
      float r = rpm->get();
      if (std::isnan(r) || r < ENGINE_RUNNING_RPM) return 0.0f;

      float max_kW = max_power->get();
      if (std::isnan(max_kW) || max_kW <= 0.0f) return 0.0f;

      float load = kW / max_kW;
      return clamp_val(load, 0.0f, 1.0f);
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
