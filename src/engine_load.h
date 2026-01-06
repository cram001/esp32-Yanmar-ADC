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
#include <sensesp/system/utils.h>                 // clamp_val
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
static constexpr float ENGINE_RUNNING_RPM    = 500.0f;

// ============================================================================
// Internal latched state (prevents NaN poisoning / async get() races)
// ============================================================================
struct EngineLoadLatchedState {
  float rpm = NAN;        // last known good RPM
  float max_kW = NAN;     // last known good max power (kW) at RPM
};

// ============================================================================
// SETUP — ENGINE LOAD
// ============================================================================
inline void setup_engine_load(
  ValueProducer<float>* rpm_rev_s,
  Transform<float,float>* fuel_lph
) {
  if (!rpm_rev_s || !fuel_lph) return;

  // Persistent state that survives beyond this function scope
  auto* state = new EngineLoadLatchedState();

  // --------------------------------------------------------------------------
  // rev/s → RPM (sanity-checked)
  // --------------------------------------------------------------------------
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      if (!std::isfinite(rps)) return NAN;
      const float r = rps * 60.0f;
      return (r < 0.0f || r > 4500.0f) ? NAN : r;
    })
  );

  // Latch last known good RPM (do NOT use rpm->get() inside other transforms)
  rpm->connect_to(new LambdaTransform<float,float>([state](float r){
    if (std::isfinite(r)) {
      state->rpm = r;
    }
    return r; // pass-through (does not change behavior)
  }));

  // --------------------------------------------------------------------------
  // Max power at RPM (CurveInterpolator may output NAN outside curve bounds)
  // --------------------------------------------------------------------------
  auto* max_power = new CurveInterpolator(&max_power_curve);
  rpm->connect_to(max_power);

  // Latch last known good max_kW
  max_power->connect_to(new LambdaTransform<float,float>([state](float mkW){
    if (std::isfinite(mkW) && mkW > 0.0f) {
      state->max_kW = mkW;
    }
    return mkW; // pass-through
  }));

  // --------------------------------------------------------------------------
  // Actual power from fuel (kW)
  //   kW = (L/h * kg/L * 1000 g/kg) / (g/kWh)
  // --------------------------------------------------------------------------
  auto* actual_power_kW =
    fuel_lph->connect_to(new LambdaTransform<float,float>(
      [](float lph){
        if (!std::isfinite(lph) || lph <= 0.0f) return 0.0f;
        return (lph * FUEL_DENSITY_KG_PER_L * 1000.0f) / BSFC_G_PER_KWH;
      })
    );

  // --------------------------------------------------------------------------
  // Load fraction (0.0–1.0), NEVER NAN
  // --------------------------------------------------------------------------
  actual_power_kW->connect_to(
    new LambdaTransform<float,float>([state](float kW){
      // Engine not running / RPM unknown → load = 0
      if (!std::isfinite(state->rpm) || state->rpm < ENGINE_RUNNING_RPM) {
        return 0.0f;
      }

      // Max power unknown/unavailable (e.g., RPM outside curve) → load = 0
      if (!std::isfinite(state->max_kW) || state->max_kW <= 0.0f) {
        return 0.0f;
      }

      const float load = kW / state->max_kW;
      return clamp_val(load, 0.0f, 1.0f);
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.load")
  );
}
