# Design Document: Fuel Consumption Estimator

## Overview

The Fuel Consumption Estimator is a SensESP-based component that calculates instantaneous fuel consumption rate for marine engines. It takes RPM as input and applies a configurable fuel consumption curve with linear interpolation to determine the current fuel rate. The component publishes the instantaneous fuel rate to SignalK for display and integration with vessel monitoring systems.

The design follows the SensESP reactive programming model, where RPM values flow through a transform pipeline that calculates fuel rates. Configuration is persisted using the SensESP configuration system, allowing users to define custom fuel curves for their specific engine characteristics.

## Architecture

The system consists of three main layers:

1. **Input Layer**: Receives RPM values from existing RPM sensors in the SensESP pipeline
2. **Processing Layer**: Contains the fuel curve interpolation logic
3. **Output Layer**: Publishes instantaneous fuel rate to SignalK paths

The component is implemented as a SensESP Transform that can be inserted into the reactive data flow pipeline. It maintains internal state for the fuel curve configuration.

### Component Diagram

```
RPM Input → FuelConsumptionEstimator → Fuel Rate Output → SignalK
```

## Components and Interfaces

### FuelCurvePoint

A simple data structure representing a single point on the fuel consumption curve.

```cpp
struct FuelCurvePoint {
    float rpm;           // Engine RPM
    float fuel_rate;     // Fuel consumption in L/h or GPH
};
```

### FuelConsumptionEstimator

The main transform class that processes RPM input and produces fuel rate output.

**Inheritance**: Extends `Transform<float, float>` to receive RPM and output instantaneous fuel rate

**Key Members**:
- `std::vector<FuelCurvePoint> fuel_curve_`: Stores the fuel consumption curve
- `SKOutputFloat* fuel_rate_output_`: SignalK output for instantaneous rate

**Key Methods**:
- `void set_input(float rpm)`: Receives RPM value and triggers calculation
- `float interpolate_fuel_rate(float rpm)`: Calculates fuel rate from curve in L/h
- `float convert_to_m3_per_sec(float liters_per_hour)`: Converts L/h to m³/s for SignalK
- `bool validate_curve()`: Validates fuel curve configuration
- `void get_configuration(JsonObject& doc)`: Serializes configuration
- `bool set_configuration(const JsonObject& doc)`: Deserializes configuration

### InterpolationEngine

A utility class or set of functions for performing linear interpolation on the fuel curve.

**Key Functions**:
- `float linear_interpolate(float x, float x0, float y0, float x1, float y1)`: Standard linear interpolation
- `size_t find_curve_segment(const std::vector<FuelCurvePoint>& curve, float rpm)`: Binary search to find relevant curve segment

## Data Models

### Fuel Curve Configuration

The fuel curve is stored as a JSON array in the SensESP configuration system:

```json
{
  "fuel_curve": [
    {"rpm": 500, "fuel_rate": 0.6},
    {"rpm": 850, "fuel_rate": 0.7},
    {"rpm": 1500, "fuel_rate": 1.8},
    {"rpm": 2000, "fuel_rate": 2.3},
    {"rpm": 2800, "fuel_rate": 2.7},
    {"rpm": 3200, "fuel_rate": 3.4},
    {"rpm": 3900, "fuel_rate": 6.5}
  ],
  "fuel_unit": "L/h"
}
```

### SignalK Paths

The component publishes to standard SignalK paths:

- **Instantaneous Rate**: `propulsion.main.fuel.rate` (m³/s)

**Unit Conversion**: The fuel curve is configured in L/h for user convenience, but the component converts to m³/s before publishing to SignalK using the conversion: `m3_per_sec = liters_per_hour / 3600000.0`

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Fuel curve validation rejects invalid curves

*For any* fuel curve configuration, if the curve has fewer than two points, contains non-ascending RPM values, or contains negative fuel rates, then validation should fail and reject the curve.
**Validates: Requirements 1.2, 1.3, 1.4**

### Property 2: Interpolation correctness across all RPM ranges

*For any* valid fuel curve and any RPM value, the interpolated fuel rate should satisfy:
- If RPM is below the minimum curve point, return the minimum fuel rate
- If RPM is above the maximum curve point, return the maximum fuel rate  
- If RPM equals a curve point exactly, return that exact fuel rate
- If RPM falls between two curve points, return a value calculated by linear interpolation: `fuel_rate = y0 + (rpm - x0) * (y1 - y0) / (x1 - x0)`

**Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5**

### Property 3: Configuration round-trip preservation

*For any* valid fuel curve configuration, serializing to JSON and then deserializing should produce an equivalent configuration with the same curve points and settings.
**Validates: Requirements 4.3**

### Property 4: SignalK output reflects calculated values

*For any* calculated fuel rate, the value published to SignalK output should match the calculated value within acceptable floating-point precision.
**Validates: Requirements 3.1**

## Error Handling

The system handles several error conditions:

1. **Invalid Fuel Curve Configuration**
   - Detection: Validation checks during initialization and configuration updates
   - Response: Log error message, use default fuel curve (idle and max RPM points)
   - Recovery: Allow user to update configuration through SensESP web interface

2. **Missing or Invalid RPM Input**
   - Detection: Check for NaN, negative values, or null input
   - Response: Publish null to SignalK output
   - Recovery: Resume normal operation when valid RPM is received

3. **Configuration Deserialization Failure**
   - Detection: JSON parsing errors or missing required fields
   - Response: Keep existing configuration, log error
   - Recovery: User must provide valid configuration

## Testing Strategy

The testing approach combines unit tests for specific scenarios and property-based tests for universal correctness guarantees.

### Unit Testing

Unit tests will cover:

- Initialization with valid and invalid configurations (examples from requirements 1.1, 1.5)
- Specific interpolation examples with known curve points
- SignalK path correctness (example from requirement 3.2)
- Error handling with invalid inputs (example from requirement 3.3)
- Configuration change application (example from requirement 4.5)

### Property-Based Testing

Property-based tests will verify the correctness properties defined above using **ArduinoUnit** with custom property test helpers for embedded C++. Since there is no mature PBT library for Arduino/ESP32, we will implement a lightweight property testing framework that:

- Generates random test cases (fuel curves, RPM values)
- Runs each property test for a minimum of 100 iterations
- Reports the first failing case if a property is violated

Each property-based test will be tagged with a comment in this format:
```cpp
// Feature: fuel-consumption-estimator, Property 1: Fuel curve validation rejects invalid curves
```

**Test Generators**:
- `generate_random_fuel_curve()`: Creates curves with 2-10 points, varying RPM ranges
- `generate_invalid_fuel_curve()`: Creates curves violating validation rules
- `generate_random_rpm()`: Produces RPM values from 0-5000

**Property Test Structure**:
```cpp
void test_property_interpolation_correctness() {
    // Feature: fuel-consumption-estimator, Property 2: Interpolation correctness across all RPM ranges
    for (int i = 0; i < 100; i++) {
        auto curve = generate_random_fuel_curve();
        float rpm = generate_random_rpm();
        float result = interpolate_fuel_rate(curve, rpm);
        
        // Verify property holds
        assertTrue(verify_interpolation_property(curve, rpm, result));
    }
}
```

### Integration Testing

Integration tests will verify:
- End-to-end data flow from RPM input through to SignalK output
- Configuration persistence and reload across system restarts
- Interaction with SensESP framework components

### Test Coverage Goals

- Unit test coverage: >80% of code paths
- Property test coverage: All 4 correctness properties implemented
- Integration test coverage: All major data flow paths

## Implementation Notes

### Performance Considerations

- Interpolation uses binary search for O(log n) lookup in fuel curve
- Fuel rate calculation is O(1) per update
- Configuration changes are infrequent, so validation overhead is acceptable

### Memory Constraints

- Fuel curve stored in heap memory (std::vector)
- Typical curve with 10 points: ~80 bytes
- Total component memory footprint: <300 bytes

### Thread Safety

- Component runs in SensESP main loop (single-threaded)
- No mutex required for internal state
- Configuration updates occur synchronously

### Extensibility

Future enhancements could include:
- Multiple fuel curves for different engine modes
- Temperature compensation for fuel density
- Fuel efficiency metrics (L/h per kW)
- Integration with fuel tank level sensors for remaining fuel calculations
