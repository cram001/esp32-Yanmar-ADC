#pragma once

/*
 * ============================================================================
 * ENGINE LOAD — kW-BASED, HARDENED (AUTHORITATIVE)
 * ============================================================================
 *
 * RULES
 * -----
 *  • Load is a STATE (never NaN)
 *  • Engine off        → load = 0
 *  • Engine running    → finite [0..1]
 *  • Brief glitches    → held / clamped
 *
 * INPUTS
 * ------
 *  • rpm_rev_s : canonical, stabilized rev/s
 *  • fuel_lph  : stabilized fuel rate (L/h), 0 = engine off
 *
 * OUTPUT
 * ------
 *  • propulsion.engine.load
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
// HELPERS
// ============================================================================
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
// SETUP — ENGINE LOAD
// ============================================================================
inline void setup_engine_load(
  ValueProducer<float>* rpm_rev_s,
  Transform<float,float>* fuel_lph
) {
  if (!rpm_rev_s || !fuel_lph) return;

  // -------------------------------------------------------------------------
  // rev/s → RPM (single canonical conversion)
  // -------------------------------------------------------------------------
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      if (std::isnan(rps)) return 0.0f;   // NEVER NaN
      float r = rps * 60.0f;
      return (r < 0.0f || r > 4500.0f) ? 0.0f : r;
    })
  );

#if ENABLE_DEBUG_OUTPUTS
  rpm->connect_to(
    new SKOutputFloat("debug.engine.rpm.load_domain")
  );
#endif

  // -------------------------------------------------------------------------
  // Max power at current RPM (kW)
  // -------------------------------------------------------------------------
  auto* max_power = new CurveInterpolator(&max_power_curve);
  rpm->connect_to(max_power);

#if ENABLE_DEBUG_OUTPUTS
  max_power->connect_to(
    new SKOutputFloat("debug.engine.maxPower_kW")
  );
#endif

  // -------------------------------------------------------------------------
  // Actual delivered power from fuel (kW)
  //   0 fuel → 0 kW (NOT NaN)
  // -------------------------------------------------------------------------
  auto* actual_power_kW =
    fuel_lph->connect_to(
      new LambdaTransform<float,float>([](float lph){
        if (std::isnan(lph) || lph <= 0.0f) {
          return 0.0f;
        }
        return (lph * FUEL_DENSITY_KG_PER_L * 1000.0f) / BSFC_G_PER_KWH;
      })
    );

#if ENABLE_DEBUG_OUTPUTS
  actual_power_kW->connect_to(
    new SKOutputFloat("debug.engine.actualPower_kW")
  );
#endif

  // -------------------------------------------------------------------------
  // Load fraction — HARDENED
  // -------------------------------------------------------------------------
  actual_power_kW->connect_to(
    new LambdaTransform<float,float>([=](float kW){

      float r = rpm->get();
      if (r < ENGINE_RUNNING_RPM) {
        return 0.0f;   // engine off
      }

      float max_kW = max_power->get();
      if (std::isnan(max_kW) || max_kW <= 0.0f) {
        return 0.0f;
      }

      float load = kW / max_kW;

      // Numerical safety
      return clamp_val(load, 0.0f, 1.0f);
    })
  )->connect_to(
    new SKOutputFloat(
      "propulsion.engine.load",
      "/config/outputs/sk/engine_load"
    )
  );
}
