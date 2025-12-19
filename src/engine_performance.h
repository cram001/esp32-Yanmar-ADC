#pragma once

// ============================================================================
// ENGINE LOAD & FUEL FLOW ESTIMATION
// ============================================================================
//
// STW enable/disable is implemented using a Linear transform as a config carrier.
// This is REQUIRED for SensESP v3.1.0.
//
// We feed a constant value (1.0) into the Linear transform via a LambdaTransform.
// The RPM input is used only to trigger updates.
//
//   output = multiplier * 1.0 + offset
//
// Interpretation:
//   output >= 0.5  → STW USED
//   output <  0.5  → STW IGNORED
//
// ============================================================================

#include <cmath>
#include <set>
#include <Arduino.h>

#include <sensesp/system/valueproducer.h>
#include <sensesp/ui/config_item.h>
#include <sensesp/ui/status_page_item.h>

#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/transforms/frequency.h>
#include <sensesp/transforms/moving_average.h>
#include <sensesp/transforms/linear.h>

#include <sensesp/signalk/signalk_output.h>

using namespace sensesp;

float stw_ignored_flag = NAN;  // 1 = ignored, 0 = used
extern StatusPageItem<float>* ui_stw_ignored;


// -----------------------------------------------------------------------------
// Status page UI outputs (defined in main.cpp)
// -----------------------------------------------------------------------------
extern StatusPageItem<float>* ui_fuel_flow_lph;
extern StatusPageItem<float>* ui_engine_load_pct;
extern StatusPageItem<float>* ui_stw_ignored;

// ============================================================================
// CONFIG CARRIER — USE STW FLAG
// ============================================================================

static Linear* use_stw_cfg = nullptr;

// Status page backing variable
static float stw_ignored_status = NAN;

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

  if (head > 0.0f) {
    penalty += strength * head * 0.30f;
  } else {
    penalty += strength * head * 0.12f;
  }

  penalty += strength * cross * 0.18f;

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
  // CONFIGURATION UI (REGISTER ONCE)
  // -------------------------------------------------------------------------
  static bool cfg_registered = false;
  if (!cfg_registered) {

    // Linear used ONLY as config carrier
    use_stw_cfg = new Linear(
      1.0f,   // multiplier (1 = use STW)
      0.0f,   // offset
      "/config/engine/use_stw"
    );

    ConfigItem(use_stw_cfg)
      ->set_title("Use STW for engine performance")
      ->set_description("Output >= 0.5 = use STW, < 0.5 = ignore STW")
      ->set_sort_order(90);

    extern float stw_ignored_flag;
    
  ui_stw_ignored = new StatusPageItem<float>(
  "STW Ignored (1=yes, 0=no)",
  0.0f,
  "Engine",
  35
);


    cfg_registered = true;
  }

  // -------------------------------------------------------------------------
  // RPM (rps → rpm)
  // -------------------------------------------------------------------------
  auto* rpm = rpm_source->connect_to(
    new LambdaTransform<float,float>([](float rps){
      float r = rps * 60.0f;
      return (r < 300.0f || r > 4500.0f) ? NAN : r;
    })
  );

  // Feed constant 1.0 into the Linear config carrier
  rpm->connect_to(
    new LambdaTransform<float,float>([](float /*rpm*/){
      return 1.0f;
    })
  )->connect_to(use_stw_cfg);

  auto* base_stw   = new CurveInterpolator(&baseline_stw_curve);
  auto* base_fuel  = new CurveInterpolator(&baseline_fuel_curve);
  auto* rated_fuel = new CurveInterpolator(&rated_fuel_curve);

  rpm->connect_to(base_stw);
  rpm->connect_to(base_fuel);
  rpm->connect_to(rated_fuel);

  // -------------------------------------------------------------------------
  // FUEL FLOW CALCULATION
  // -------------------------------------------------------------------------
  auto* fuel_lph_raw = rpm->connect_to(
    new LambdaTransform<float,float>([=](float r){

      float baseFuel = base_fuel->get();
      float baseSTW  = base_stw->get();
      float fuelMax  = rated_fuel->get();

      if (std::isnan(baseFuel) || baseFuel <= 0.0f) return NAN;

      bool use_stw = (use_stw_cfg->get() >= 0.5f);
      // expose status-friendly value
      stw_ignored_flag = use_stw ? 0.0f : 1.0f;
      if (ui_stw_ignored) {
         ui_stw_ignored->set(use_stw ? 0.0f : 1.0f);
}

      float stw_factor = 1.0f;

      if (use_stw && stw_knots) {
        float stw = stw_knots->get();
        if (!std::isnan(stw) && stw > 0.0f && !stw_invalid(r, stw)) {
          float ratio = baseSTW / stw;
          if (ratio >= 1.05f) {
            stw_factor = powf(ratio, 0.6f);
            stw_factor = clamp_val(stw_factor, 1.0f, 2.0f);
          }
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

      if (!std::isnan(fuelMax)) {
        fuel = std::min(fuel, fuelMax);
      }

      return clamp_val(fuel, 0.0f, 14.0f);
    })
  );

  // -------------------------------------------------------------------------
  // SMOOTHING
  // -------------------------------------------------------------------------
  auto* fuel_lph = fuel_lph_raw->connect_to(new MovingAverage(2));


// ---------------------------------------------------------------------------
// STATUS PAGE — Fuel flow (L/hr)
// ---------------------------------------------------------------------------
  ui_fuel_flow_lph = new StatusPageItem<float>(
    "Fuel Flow (L/hr)",
    0.0f,
    "Engine",
    30
  );

  // Insert into pipeline
  fuel_lph->connect_to(ui_fuel_flow_lph);


  
  // Fuel → m³/s
  fuel_lph->connect_to(
    new LambdaTransform<float,float>([](float lph){
      return std::isnan(lph) ? NAN : (lph / 1000.0f) / 3600.0f;
    })
  )->connect_to(
    new SKOutputFloat("propulsion.engine.fuel.rate")
  );



  // Engine load (%)
  

  auto* engine_load = fuel_lph->connect_to(
  new LambdaTransform<float,float>([=](float lph){
    float fmax = rated_fuel->get();
    if (std::isnan(lph) || std::isnan(fmax) || fmax <= 0.0f) return NAN;
    return clamp_val(lph / fmax, 0.0f, 1.0f);
  })
);

// ---------------------------------------------------------------------------
// STATUS PAGE — Engine load (%)
// ---------------------------------------------------------------------------
ui_engine_load_pct = new StatusPageItem<float>(
  "Engine Load (%)",
  0.0f,
  "Engine",
  40
);

// Convert fraction → percent for UI
engine_load->connect_to(
  new LambdaTransform<float,float>([](float frac){
    float pct = std::isnan(frac) ? NAN : frac * 100.0f;
    if (ui_engine_load_pct) {
      ui_engine_load_pct->set(pct);
    }
    return frac;
  })
);

// Signal K (fraction 0–1)
engine_load->connect_to(
  new SKOutputFloat("propulsion.engine.load")
);


  
}
