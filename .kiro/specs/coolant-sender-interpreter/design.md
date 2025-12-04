# Design Document: Marine 450-30 Ohm Coolant Sender Interpreter

## Overview

The Marine 450-30 Ohm Coolant Sender Interpreter is a software component that converts analog resistance measurements from an American D type marine coolant temperature sender into accurate digital temperature readings. The system operates within the SensESP framework on an ESP32 microcontroller with 4MB flash memory (half of which is available due to use of OTA updates), processing ADC voltage inputs through multiple stages: voltage divider compensation, series circuit analysis, resistance-to-temperature mapping, and signal filtering.

The interpreter must handle the complexity of measuring sender resistance while the sender is connected in series with an analog gauge, requiring compensation for both the gauge coil resistance and an external voltage divider used to protect the ESP32 ADC. The design emphasizes robustness through comprehensive input validation, noise rejection via median and moving average filters, and graceful handling of fault conditions.

## Architecture

The system follows a pipeline architecture with the following stages:

```
ADC Input → Voltage Divider Compensation → Series Circuit Analysis → 
Resistance Validation → Temperature Mapping → Signal Filtering → Output
```

### Component Flow

1. **ADC Input Stage**: Receives raw voltage from ESP32 ADC (0-2.5V range with DB_11 attenuation)
2. **Voltage Divider Compensation**: Multiplies ADC voltage by 5.0× to reverse the DFRobot 30kΩ/7.5kΩ divider
3. **Series Circuit Analysis**: Calculates sender resistance using the formula: Rsender = Rgauge × (Vtap / (Vsupply - Vtap))
4. **Resistance Validation**: Checks for valid resistance range (20Ω to 20,000Ω) and rejects fault conditions
5. **Temperature Mapping**: Converts resistance to temperature using 10-point American D type calibration curve with interpolation
6. **Signal Filtering**: Applies median filter (5 samples) and moving average (50 samples @ 10Hz)
7. **Output Stage**: Emits temperature in Celsius, converts to Kelvin for SignalK transmission

### Integration Points

- **SensESP Framework**: Inherits from `sensesp::Transform<float, float>` for pipeline integration
- **SignalK Protocol**: Outputs to `propulsion.mainEngine.coolantTemperature` path
- **Configuration System**: Exposes calibration parameters through SensESP config UI

## Components and Interfaces

### SenderResistance Class

**Purpose**: Core transform that converts ADC voltage to sender resistance

**Interface**:
```cpp
class SenderResistance : public sensesp::Transform<float, float> {
public:
  SenderResistance(float supply_voltage, float r_gauge_coil);
  void set(const float& vadc) override;
  
private:
  float supply_voltage_;    // 12.0-14.4V nominal
  float r_gauge_coil_;      // ~1180Ω typical
};
```

**Inputs**:
- `vadc`: ADC voltage reading (0-2.5V after calibration)

**Outputs**:
- Sender resistance in Ohms (20-20,000Ω valid range)
- NaN for invalid/fault conditions

**Configuration Parameters**:
- `supply_voltage`: Battery/gauge supply voltage (default: 13.5V)
- `r_gauge_coil`: Internal resistance of analog gauge (default: 1180Ω)

### CurveInterpolator Integration

**Purpose**: Maps sender resistance to temperature using American D type curve

**Calibration Table** (Resistance → Temperature):
```
29.6Ω   → 121.0°C
45.0Ω   → 100.0°C
85.5Ω   → 85.0°C
90.9Ω   → 82.5°C
97.0Ω   → 80.0°C
104.0Ω  → 76.7°C
112.0Ω  → 72.0°C
131.0Ω  → 63.9°C
207.0Ω  → 56.0°C
1352.0Ω → 12.7°C
```

**Interpolation Method**: Linear interpolation between calibration points

### Filtering Components

**Median Filter**:
- Window size: 5 samples
- Purpose: Reject ADC spike noise and electrical interference
- Position: Applied to sender voltage after divider compensation

**Moving Average Filter**:
- Window size: 50 samples
- Sample rate: 10Hz
- Duration: 5 seconds
- Purpose: Smooth temperature fluctuations for stable display

## Data Models

### Voltage Divider Model

The DFRobot voltage divider uses a 30kΩ/7.5kΩ resistor network:

```
Vin ──[30kΩ]──┬──[7.5kΩ]── GND
              │
            Vout (to ADC)
```

**Divider Ratio**: (R1 + R2) / R2 = (30000 + 7500) / 7500 = 5.0

**Compensation Formula**: Vtap_actual = Vadc × 5.0

### Series Circuit Model

The gauge and sender form a series circuit with the battery:

```
+12V ──[Rgauge]──┬──[Rsender]── GND
                 │
              Vtap (measurement point)
```

**Voltage Division**: Vtap = Vsupply × (Rsender / (Rgauge + Rsender))

**Solving for Rsender**: Rsender = Rgauge × (Vtap / (Vsupply - Vtap))

### Temperature Curve Model

The American D type sender follows a non-linear resistance-temperature relationship. The curve is defined by 10 calibration points spanning the operating range from 12.7°C (cold) to 121°C (hot). Temperature values between calibration points are determined through linear interpolation.

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property Reflection

After analyzing all acceptance criteria, the following redundancies were identified and eliminated:

- **Redundancy 1**: Requirements 1.4 and 2.1 both test series circuit analysis → Combined into Property 2
- **Redundancy 2**: Requirements 1.3 and 8.2 both test Celsius to Kelvin conversion → Combined into Property 3
- **Redundancy 3**: Requirements 3.3, 3.4, and 3.5 all test invalid input rejection → Combined into Property 5
- **Redundancy 4**: Requirements 6.2 and 6.3 both test emit behavior → Combined into Property 8

The following properties provide comprehensive, non-redundant validation:

### Property 1: Voltage divider compensation correctness
*For any* valid ADC voltage input, applying the voltage divider compensation should multiply the voltage by exactly 5.0
**Validates: Requirements 1.5, 2.4**

### Property 2: Series circuit resistance calculation
*For any* valid tap voltage, supply voltage, and gauge resistance, the calculated sender resistance should satisfy the formula: Rsender = Rgauge × (Vtap / (Vsupply - Vtap))
**Validates: Requirements 1.1, 1.4, 2.1, 2.2**

### Property 3: Temperature unit conversion
*For any* valid temperature in Celsius, the output to SignalK should equal the Celsius value plus 273.15
**Validates: Requirements 1.3, 8.2**

### Property 4: Supply voltage independence
*For any* supply voltage between 12V and 14.4V, given the same sender resistance, the calculated temperature should remain within ±0.5°C
**Validates: Requirements 2.3**

### Property 5: Invalid input rejection
*For any* input that produces physically impossible conditions (tap voltage > supply voltage, resistance < 20Ω, resistance > 20,000Ω, NaN, or infinite values), the output should be NaN
**Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5**

### Property 6: Curve interpolation monotonicity
*For any* two resistance values R1 < R2 within the calibration range, the temperature T1 corresponding to R1 should be greater than or equal to T2 corresponding to R2 (since resistance decreases as temperature increases)
**Validates: Requirements 1.2, 4.3**

### Property 7: Calibration factor linearity
*For any* sender resistance and calibration factor, applying the calibration factor should scale the resistance by exactly that factor before temperature lookup
**Validates: Requirements 5.1**

### Property 8: Transform emission consistency
*For any* input value, the transform should emit exactly one output value (either valid resistance or NaN), and the emitted value should be accessible to connected transforms
**Validates: Requirements 6.2, 6.3**

### Property 9: Median filter spike rejection
*For any* sequence of voltage readings where one value is an outlier (differs by >50% from median), the median filter output should not equal the outlier value
**Validates: Requirements 7.1**

### Property 10: Moving average smoothing
*For any* sequence of temperature values with variance σ², the moving average output should have variance ≤ σ²/N where N is the window size
**Validates: Requirements 7.3**

### Property 11: NaN propagation
*For any* NaN temperature value, the system should not transmit to SignalK (transmission should be skipped)
**Validates: Requirements 8.3**

### Property 12: Configuration parameter usage
*For any* configured supply voltage or gauge resistance value, changing the configuration should change the calculated sender resistance for the same ADC input
**Validates: Requirements 5.2, 5.3**

## Error Handling

### Input Validation Errors

1. **NaN or Infinite Input**: Immediately return NaN without processing
2. **Low Voltage (<0.01V)**: Treat as floating/disconnected input, return NaN
3. **Impossible Voltage**: If Vtap ≥ Vsupply - 0.05V, return NaN
4. **Division by Zero**: If Vsupply - Vtap ≤ 0.01V, return NaN

### Resistance Range Errors

1. **Short Circuit (<20Ω)**: Indicates wiring fault or sender failure, return NaN
2. **Open Circuit (>20,000Ω)**: Indicates disconnected sender or impossible value, return NaN

### Temperature Mapping Errors

1. **Out of Range Resistance**: CurveInterpolator will extrapolate beyond calibration points
2. **Interpolation Failure**: Should not occur with valid resistance input

### Error Propagation

All error conditions result in NaN output, which propagates through the pipeline:
- Median filter passes NaN through
- Moving average handles NaN by excluding from calculation
- SignalK output skips transmission when value is NaN

## Testing Strategy

### Unit Testing Approach

Unit tests will verify specific examples and edge cases:

1. **Calibration Point Tests**: Verify exact temperature output for each of the 10 calibration points
2. **Divider Scaling Test**: Verify 5.0× scaling factor is applied correctly
3. **Edge Case Tests**: Verify NaN output for boundary conditions (0.01V, 20Ω, 20,000Ω)
4. **Filter Configuration Tests**: Verify median window size is 5 and moving average is 50 samples

### Property-Based Testing Approach

Property-based tests will verify universal behaviors across all inputs using **fast-check** (JavaScript/TypeScript PBT library):

**Test Configuration**: Each property test will run a minimum of 100 iterations with randomly generated inputs.

**Generator Strategy**:
- **Valid ADC voltages**: 0.01V to 2.5V
- **Supply voltages**: 12.0V to 14.4V
- **Gauge resistances**: 1000Ω to 1500Ω (typical range)
- **Sender resistances**: 20Ω to 2000Ω (operating range)
- **Calibration factors**: 0.8 to 1.2 (typical adjustment range)

**Property Test Tagging**: Each property-based test will include a comment tag in the format:
```cpp
// **Feature: coolant-sender-interpreter, Property N: [property description]**
// **Validates: Requirements X.Y**
```

### Integration Testing

Integration tests will verify the complete pipeline:

1. **End-to-End Temperature Conversion**: ADC input → temperature output
2. **Filter Chain**: Verify median → moving average → output sequence
3. **SignalK Integration**: Verify correct path and unit conversion
4. **Configuration Changes**: Verify parameter updates affect calculations

### Test Data Sources

1. **Manufacturer Specifications**: American D type sender curve data
2. **Field Measurements**: Actual voltage readings from installed systems
3. **Boundary Conditions**: Physical limits of ADC, voltage divider, and sender

### Testing Tools

- **Unit Test Framework**: Google Test (C++) or Jest (if testing TypeScript port)
- **Property-Based Testing**: fast-check (JavaScript/TypeScript)
- **Mocking**: Minimal mocking - prefer testing real calculation logic
- **Coverage Target**: 90%+ code coverage, 100% of error paths

### Test Execution Strategy

1. Run unit tests first to catch basic functionality issues
2. Run property tests to verify universal behaviors
3. Run integration tests to verify pipeline operation
4. Manual testing on hardware for final validation

The dual testing approach ensures both specific examples work correctly (unit tests) and general behaviors hold across all inputs (property tests), providing comprehensive validation of the coolant sender interpreter.
