#pragma once

#include <sensesp/transforms/curveinterpolator.h>
#include <sensesp/transforms/lambda_transform.h>
#include <sensesp/signalk/signalk_output.h>

using namespace sensesp;

// ============================================================================
// ENGINE LOAD MODULE for Yanmar 3JH3E EPA
//
// Computes:
//   • Actual engine power (kW) from fuel burn
//   • Max possible power at current RPM (kW)
//   • Engine load fraction (0–1)
//
// Required:
//   • rpm_source → Frequency* (produces revolutions per second)
//   • fuel_lph  → Transform<float,float>* (L/h from your fuel curve)
//
// Publishes to Signal K:
//   propulsion.mainEngine.load
//   debug.engine.power_kW
//   debug.engine.maxPower_kW
// ============================================================================


// ---- MAX POWER CURVE (kW) ----
// Based on Yanmar 3JH3E EPA fuel sheet (40 hp @ 3800 rpm → 29.83 kW)
static std::set<CurveInterpolator::Sample> max_power_curve = {
    {1800, 17.9f},
    {2000, 20.9f},
    {2400, 24.6f},
    {2800, 26.8f},
    {3200, 28.3f},
    {3600, 29.5f},
    {3800, 29.83f}
};


// ---- CONSTANTS ----
static constexpr float BSFC_G_PER_KWH       = 240.0f;  // typical g/kWh
static constexpr float FUEL_DENSITY_KG_PER_L = 0.84f;  // diesel density


// ============================================================================
// setup_engine_load()
// ============================================================================
inline void setup_engine_load(
        Frequency* rpm_source,
        Transform<float, float>* fuel_lph) {

    //
    // 1) MAX POWER AT RPM (kW)
    //
    auto* max_power_interp =
        new CurveInterpolator(&max_power_curve,
                              "/config/engine/max_power_curve");

    rpm_source->connect_to(max_power_interp);


    //
    // 2) ACTUAL POWER FROM FUEL RATE (kW)
    //
    auto* actual_power_kW =
        fuel_lph->connect_to(new LambdaTransform<float, float>(
            [](float lph) -> float {

                if (std::isnan(lph) || lph <= 0.0f) {
                    return NAN;
                }

                // Convert L/h → kg/h
                float fuel_kg_per_h = lph * FUEL_DENSITY_KG_PER_L;

                // P (kW) = (kg/h * 1000 g/kg) / (g/kWh)
                float kW = (fuel_kg_per_h * 1000.0f) / BSFC_G_PER_KWH;

                return kW;
            },
            "/config/engine/actual_power_kW"
        ));


    //
    // 3) ENGINE LOAD FRACTION = actual kW / max kW
    //
    auto* engine_load =
        actual_power_kW->connect_to(new LambdaTransform<float, float>(
            [max_power_interp, rpm_source](float actual_kW) -> float {

                if (std::isnan(actual_kW)) {
                    return NAN;
                }

                float max_kW = max_power_interp->get();
                if (std::isnan(max_kW) || max_kW <= 0.0f) {
                    return NAN;
                }

                return actual_kW / max_kW;   // 0.0 → 1.0
            },
            "/config/engine/load_fraction"
        ));


    //
    // 4) SIGNAL K OUTPUTS
    //

    // Engine load (0–1)
    engine_load->connect_to(
        new SKOutputFloat("propulsion.mainEngine.load",
                          "/config/outputs/sk/engine_load")
    );

    // Debug outputs (conditional compilation)
#if ENABLE_DEBUG_OUTPUTS
    // Actual delivered power
    actual_power_kW->connect_to(
        new SKOutputFloat("debug.engine.power_kW")
    );

    // Max power at current RPM
    max_power_interp->connect_to(
        new SKOutputFloat("debug.engine.maxPower_kW")
    );
#endif
}
