#pragma once

/*
 * ============================================================================
 * ENGINE FUEL FLOW ESTIMATION — STABILIZED
 * ============================================================================
 *
 * RULES
 * -----
 *  • Fuel flow is a STATE, not a measurement
 *  • While engine is running:
 *      - fuel is NEVER NaN
 *      - short RPM glitches are ignored
 *  • Engine off → fuel = 0
 *
 *  • RPM input: rev/s (smoothed upstream)
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
constexpr float ENGINE_RUNNING_RPM = 500.0f;
constexpr float DOCK_SPEED_KTS     = 0.15f;
constexpr float MS_TO_KTS          = 1.94384449f;

// RPM glitch tolerance
constexpr unsigned long RPM_HOLD_MS = 700;

// ============================================================================
// HELPERS
// ============================================================================
template <typename T>
static inline T clamp_val(T v, T lo, T hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ============================================================================
// CURVES
// ============================================================================
static std::set<CurveInterpolator::Sample> baseline_fuel_curve = {
  {  500, 0.60 },
  { 1000, 0.75 },
  { 1800, 1.10 },
  { 2000, 1.50 },
  { 2200, 1.85 },
  { 2400, 2.25 },
  { 2500, 2.60 },
  { 2800, 3.60 },
  { 3000, 4.40 },
  { 3200, 5.30 },
  { 3600, 6.90 },
  { 3800, 7.60 },
  { 3900, 9.60 }
};

static std::set<CurveInterpolator::Sample> idle_fuel_curve = {
  { 800, 0.6 },
  { 3600, 1.4 }
};

// ============================================================================
// CONFIG
// ============================================================================
static Linear* use_stw_cfg = nullptr;

// ============================================================================
// SETUP — ENGINE FUEL (STABILIZED)
// ============================================================================
inline Transform<float,float>* setup_engine_fuel(
  ValueProducer<float>* rpm_rev_s,
  ValueProducer<float>* stw_ms,
  ValueProducer<float>* sog_ms
) {
  if (!rpm_rev_s) return nullptr;

  // Config UI
  if (!use_stw_cfg) {
    use_stw_cfg = new Linear(1.0f, 0.0f, "/config/engine_fuel/use_stw");
    ConfigItem(use_stw_cfg)
      ->set_title("Use STW for fuel model");
  }

  // rev/s → RPM
  auto* rpm = rpm_rev_s->connect_to(
    new LambdaTransform<float,float>([](float rps){
      return std::isnan(rps) ? NAN : rps * 60.0f;
    })
  );

  auto* base_fuel = new CurveInterpolator(&baseline_fuel_curve);
  auto* idle_fuel = new CurveInterpolator(&idle_fuel_curve);

  rpm->connect_to(base_fuel);
  rpm->connect_to(idle_fuel);

  // -------------------------------------------------------------------------
  // STABILIZED fuel model (L/h)
  // -------------------------------------------------------------------------
  auto* fuel_lph_raw = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      static float last_fuel = 0.0f;
      static unsigned long last_valid_rpm_ms = 0;
      static bool engine_running = false;

      unsigned long now = millis();

      // Valid RPM?
      if (!std::isnan(r) && r >= ENGINE_RUNNING_RPM) {
        engine_running = true;
        last_valid_rpm_ms = now;
      }

      // RPM missing but within hold window → keep running
      if (engine_running && (now - last_valid_rpm_ms) < RPM_HOLD_MS) {
        // still running
      } else if (std::isnan(r) || r < ENGINE_RUNNING_RPM) {
        engine_running = false;
        last_fuel = 0.0f;
        return 0.0f;
      }

      // Engine running → compute fuel
      float fuel = NAN;

      bool docked = false;
      if (use_stw_cfg->get() >= 0.5f) {
        float stw = stw_ms ? stw_ms->get() * MS_TO_KTS : NAN;
        float sog = sog_ms ? sog_ms->get() * MS_TO_KTS : NAN;
        if ((!std::isnan(stw) && stw < DOCK_SPEED_KTS) ||
            (!std::isnan(sog) && sog < DOCK_SPEED_KTS)) {
          docked = true;
        }
      }

      if (docked) {
        fuel = idle_fuel->get();
      } else {
        fuel = base_fuel->get();
      }

      if (std::isnan(fuel) || fuel <= 0.0f) {
        fuel = last_fuel;   // HOLD LAST GOOD VALUE
      }

      last_fuel = fuel;
      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  // Smooth for display only
  auto* fuel_lph = fuel_lph_raw->connect_to(new MovingAverage(2));

  // Signal K output (m³/s) — NEVER NaN
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return (lph <= 0.0f) ? 0.0f : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );

  return fuel_lph_raw;
}
