# Design Document

## Overview

The Engine Load Calculator is a SensESP-based module that computes real-time engine load percentage for the Yanmar 3JH3E EPA marine diesel engine. The system derives actual power output from fuel consumption measurements and compares it against the maximum available power at the current RPM (obtained from the manufacturer's power curve). The result is a load fraction (0.0 to 1.0) that indicates how hard the engine is working relative to its capabilities.

The design leverages existing SensESP transform components and integrates seamlessly with the current sensor pipeline, requiring only RPM (via Frequency transform) and fuel flow rate (L/h) as inputs.

## Architecture

The system follows a functional pipeline architecture using SensESP's transform pattern:

```
RPM Source (Frequency*) ──┬──> Max Power Interpolator ──┐
                          │                              │
                          └──> RPM Validation ──────────┤
                                                          ├──> Load Calculator ──> SignalK Output
Fuel Flow (L/h) ──> Actual Power Calculator ────────────┘
```

### Component Flow

1. **RPM Input**: Frequency transform provides revolutions per second (Hz)
2. **Max Power Lookup**: CurveInterpolator maps RPM to maximum power (kW) using Yanmar 3JH3E curve
3. **Fuel Flow Input**: Transform provides fuel consumption in liters per hour
4. **Actual Power Calculation**: Lambda transform converts fuel rate to actual power output (kW)
5. **Load Calculation**: Lambda transform computes load fraction = actual_power / max_power
6. **SignalK Output**: SKOutputFloat publishes load to "propulsion.mainEngine.load"

## Components and Interfaces

### 1. setup_engine_load() Function

**Purpose**: Initialize and wire the engine load calculation pipeline

**Signature**:
```cpp
void setup_engine_load(
    Frequency* rpm_source,
    Transform<float, float>* fuel_lph
);
```

**Parameters**:
- `rpm_source`: Pointer to Frequency transform (produces Hz, revolutions per second)
- `fuel_lph`: Pointer to fuel flow transform (produces L/h)

**Responsibilities**:
- Create max power interpolator
- Create actual power calculator
- Create load fraction calculator
- Wire transforms together
- Configure SignalK outputs

### 2. Max Power Interpolator

**Type**: `CurveInterpolator`

**Input**: RPM (as Hz from Frequency transform)

**Output**: Maximum power in kW

**Configuration**: Uses Yanmar 3JH3E power curve with 7 calibration points

**Behavior**:
- Interpolates linearly between curve points
- Handles RPM values outside curve range

### 3. Actual Power Calculator

**Type**: `LambdaTransform<float, float>`

**Input**: Fuel rate in L/h

**Output**: Actual power in kW

**Formula**:
```
fuel_kg_per_h = fuel_lph × FUEL_DENSITY_KG_PER_L
power_kW = (fuel_kg_per_h × 1000) / BSFC_G_PER_KWH
```

**Constants**:
- `BSFC_G_PER_KWH = 240.0f` (grams per kilowatt-hour)
- `FUEL_DENSITY_KG_PER_L = 0.84f` (diesel density)

**Error Handling**:
- Returns NaN if fuel rate is NaN
- Returns NaN if fuel rate ≤ 0

### 4. Load Fraction Calculator

**Type**: `LambdaTransform<float, float>`

**Input**: Actual power in kW

**Output**: Load fraction (0.0 to 1.0)

**Formula**:
```
load = actual_power_kW / max_power_kW
```

**Dependencies**:
- Reads max_power from max power interpolator via `get()`
- Reads RPM from rpm_source to validate engine is running

**Error Handling**:
- Returns NaN if actual power is NaN
- Returns NaN if max power is NaN or ≤ 0
- Returns NaN if RPM < 750 (engine not running)

### 5. SignalK Output

**Type**: `SKOutputFloat`

**Path**: `"propulsion.mainEngine.load"`

**Configuration Path**: `"/config/outputs/sk/engine_load"`

**Value Range**: 0.0 to 1.0 (unitless fraction)

## Data Models

### Power Curve Data Structure

```cpp
std::set<CurveInterpolator::Sample> max_power_curve = {
    {1800, 17.9f},   // RPM, kW
    {2000, 20.9f},
    {2400, 24.6f},
    {2800, 26.8f},
    {3200, 28.3f},
    {3600, 29.5f},
    {3800, 29.83f}   // Rated power: 40 hp @ 3800 RPM
};
```

### Constants

```cpp
static constexpr float BSFC_G_PER_KWH = 240.0f;        // Brake specific fuel consumption
static constexpr float FUEL_DENSITY_KG_PER_L = 0.84f;  // Diesel density
static constexpr float IDLE_RPM_THRESHOLD = 750.0f;    // Minimum RPM for valid load
```

### Data Flow Types

| Stage | Type | Unit | Range | Notes |
|-------|------|------|-------|-------|
| RPM Input | float | Hz | 0 - 70 | Revolutions per second |
| RPM (converted) | float | RPM | 0 - 4200 | Hz × 60 |
| Max Power | float | kW | 0 - 30 | From power curve |
| Fuel Rate | float | L/h | 0 - 10 | Liters per hour |
| Fuel Rate (mass) | float | kg/h | 0 - 8.4 | L/h × 0.84 |
| Actual Power | float | kW | 0 - 35 | From fuel consumption |
| Load Fraction | float | - | 0.0 - 1.0 | Unitless ratio |


## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Load calculation formula and range invariant

*For any* valid actual power and maximum power values (both positive, non-NaN), the calculated load should equal actual_power / max_power AND the result should be in the range [0.0, 1.0]

**Validates: Requirements 1.1, 1.2**

### Property 2: Fuel-to-power conversion formula

*For any* positive fuel rate in L/h, the actual power calculation should equal (fuel_lph × 0.84 × 1000) / 240.0

**Validates: Requirements 2.2, 2.3**

### Property 3: Power curve interpolation linearity

*For any* two adjacent points in the power curve, any RPM value between them should produce a power value that lies on the line segment connecting those two points

**Validates: Requirements 3.2**

### Property 4: Transform integration

*For any* connected RPM and fuel flow transforms, when those transforms have valid values, the load calculator should successfully read and use those values in its calculations

**Validates: Requirements 4.3, 4.4**

### Property 5: NaN propagation

*For any* input where either RPM source or fuel flow source outputs NaN, the final engine load output should be NaN

**Validates: Requirements 1.5, 4.5**

### Property 6: SignalK output correctness

*For any* calculated load value (including NaN), the value transmitted to SignalK path "propulsion.mainEngine.load" should match the calculated value

**Validates: Requirements 1.3, 7.3**

### Property 7: Power curve lookup accuracy

*For any* RPM value that exactly matches a calibration point in the power curve, the returned maximum power should exactly match the corresponding power value in the curve

**Validates: Requirements 3.1**

## Error Handling

The system implements defensive error handling at multiple levels:

### Input Validation

1. **Fuel Rate Validation**:
   - Check for NaN before processing
   - Check for zero or negative values
   - Return NaN for invalid inputs

2. **Power Validation**:
   - Check max power for NaN, zero, or negative
   - Check actual power for NaN
   - Prevent division by zero

3. **RPM Validation**:
   - Check for RPM below idle threshold (750 RPM)
   - Return NaN when engine is not running

### Error Propagation

The system follows a "fail-safe" approach where any invalid input results in NaN output:

```
Invalid Input → NaN → Propagates through pipeline → NaN output to SignalK
```

This ensures that:
- Sensor failures don't produce misleading data
- Display systems can detect and handle invalid readings
- The system never crashes due to invalid calculations

### Edge Cases

1. **Low RPM**: When RPM < 750, output NaN (engine not running)
2. **High RPM**: When RPM > 3800, use maximum rated power (29.83 kW)
3. **Zero fuel**: When fuel rate ≤ 0, output NaN
4. **Division by zero**: When max power ≤ 0, output NaN
5. **Infinite values**: Convert any infinite result to NaN

## Testing Strategy

The testing approach combines unit tests for specific scenarios and property-based tests for universal correctness properties.

### Property-Based Testing

Property-based testing will be used to verify the correctness properties defined above. We will use a C++ property-based testing library suitable for embedded systems.

**Library Selection**: RapidCheck or similar lightweight PBT library for C++

**Test Configuration**:
- Minimum 100 iterations per property test
- Random input generation within valid ranges
- Edge case generation for boundary conditions

**Property Test Requirements**:
- Each property test MUST be tagged with: `**Feature: engine-load-calculator, Property {number}: {property_text}**`
- Each property MUST be implemented by a SINGLE property-based test
- Tests should run as part of the PlatformIO test suite

### Unit Testing

Unit tests will cover:
- Specific calibration point verification (power curve)
- SignalK path configuration
- Edge cases (low RPM, zero fuel, NaN inputs)
- Integration with mock transforms

**Test Cases**:
1. Verify power curve calibration points return exact values
2. Verify SignalK output path is "propulsion.mainEngine.load"
3. Verify RPM below 750 returns NaN
4. Verify zero/negative fuel returns NaN
5. Verify NaN inputs propagate to NaN output
6. Verify max power ≤ 0 returns NaN

### Test Environment

- **Platform**: PlatformIO native test environment
- **Framework**: Unity or Google Test
- **Mocking**: Mock Frequency and Transform objects for isolated testing
- **Coverage**: Aim for >90% code coverage on calculation logic

### Integration Testing

Integration tests will verify:
- Correct wiring of transform pipeline
- Data flow from RPM/fuel sources to SignalK output
- Behavior with real SensESP event loop
- Configuration persistence

