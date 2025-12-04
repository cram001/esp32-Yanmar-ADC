# Design Document: Marine Engine RPM Interpreter

## Overview

The Marine Engine RPM Interpreter is a software component that converts digital pulse signals from a magnetic pickup sensor into accurate engine RPM readings. The system operates within the SensESP framework on an ESP32 microcontroller, using interrupt-driven pulse detection to measure the frequency of gear teeth passing a magnetic sensor mounted on a 116-tooth ring gear.

The interpreter must handle pulse frequencies ranging from approximately 1160 Hz (600 RPM idle) to 7560 Hz (3900 RPM maximum), with sub-second response times and robust noise rejection. The design emphasizes efficient interrupt handling, accurate frequency measurement using pulse interval timing, comprehensive input validation, and graceful handling of signal loss conditions.

## Architecture

The system follows an interrupt-driven architecture with the following stages:

```
Magnetic Pickup → Op-Amp → Opto-Isolator → ESP32 GPIO → 
ISR (Edge Detection) → Pulse Interval Measurement → 
Frequency Calculation → RPM Conversion → Signal Filtering → Output
```

### Component Flow

1. **Hardware Signal Path**: Magnetic pickup → op-amp conditioning → opto-isolator → ESP32 digital input
2. **Interrupt Service Routine**: Captures pulse timestamps on rising/falling edges with minimal processing
3. **Pulse Interval Measurement**: Calculates time between consecutive pulses using microsecond timestamps
4. **Frequency Calculation**: Converts pulse interval to frequency (Hz) using: frequency = 1,000,000 / interval_us
5. **RPM Conversion**: Converts frequency to RPM using: RPM = (frequency × 60) / 116
6. **Signal Filtering**: Applies median filter (5 samples) to pulse intervals and moving average (15 samples) to RPM
7. **Output Stage**: Emits RPM in revolutions per second (Hz), converts to Hz for SignalK transmission

### Integration Points

- **SensESP Framework**: Uses SensESP sensor base classes for integration
- **SignalK Protocol**: Outputs to `propulsion.mainEngine.revolutions` path
- **Configuration System**: Exposes calibration parameters through SensESP config UI
- **ESP32 Hardware Interrupts**: Uses attachInterrupt() for efficient pulse detection

## Components and Interfaces

### RPMSensor Class

**Purpose**: Core sensor class that captures pulses and calculates RPM

**Interface**:
```cpp
class RPMSensor : public sensesp::Sensor {
public:
  RPMSensor(uint8_t pin, uint16_t teeth_per_revolution, 
            float calibration_multiplier = 1.0);
  void enable() override;
  
private:
  static void IRAM_ATTR handleInterrupt();
  void calculateRPM();
  
  uint8_t pin_;
  uint16_t teeth_per_revolution_;
  float calibration_multiplier_;
  
  static volatile uint32_t last_pulse_time_us_;
  static volatile uint32_t pulse_interval_us_;
  static volatile bool new_pulse_available_;
};
```

**Inputs**:
- Digital pulses on configured GPIO pin (interrupt-driven)

**Outputs**:
- RPM value (0-4200 RPM valid range)
- 0 for engine stopped
- NaN for invalid/fault conditions

**Configuration Parameters**:
- `pin`: GPIO pin number for pulse input (default: GPIO 34)
- `teeth_per_revolution`: Number of teeth on ring gear (default: 116)
- `calibration_multiplier`: Fine-tuning multiplier (default: 1.0)

### Interrupt Service Routine (ISR)

**Purpose**: Capture pulse timestamps with minimal latency

**Implementation Strategy**:
```cpp
static void IRAM_ATTR handleInterrupt() {
  uint32_t current_time = micros();
  pulse_interval_us_ = current_time - last_pulse_time_us_;
  last_pulse_time_us_ = current_time;
  new_pulse_available_ = true;
}
```

**Key Characteristics**:
- Marked with `IRAM_ATTR` to execute from RAM for speed
- Uses `volatile` variables for ISR-to-main-thread communication
- Executes in <10 microseconds
- No blocking operations or complex calculations
- Thread-safe flag (`new_pulse_available_`) for main loop processing

### Frequency Calculation Component

**Purpose**: Convert pulse intervals to frequency measurements

**Calculation Method**: Interval-based frequency measurement
```
frequency_hz = 1,000,000 / pulse_interval_us
```

**Advantages over counting-based methods**:
- Faster response time (updates on every pulse)
- Better accuracy at low RPM
- Simpler implementation
- No need for time window management

**Validation**:
- Reject intervals < 100 μs (>10 kHz, above maximum expected)
- Reject intervals > 2,000,000 μs (2 seconds, engine stopped)
- Handle first pulse after startup (no previous interval)

### RPM Conversion Component

**Purpose**: Convert pulse frequency to engine RPM

**Formula**:
```
RPM = (pulse_frequency_hz × 60) / teeth_per_revolution
RPM = (pulse_frequency_hz × 60) / 116
```

**Example Calculations**:
- Idle (600 RPM): 1160 Hz → (1160 × 60) / 116 = 600 RPM
- Cruise (2000 RPM): 3867 Hz → (3867 × 60) / 116 = 2000 RPM
- Maximum (3900 RPM): 7540 Hz → (7540 × 60) / 116 = 3900 RPM

**Calibration Multiplier**:
```
RPM_final = RPM_calculated × calibration_multiplier
```

### Filtering Components

**Median Filter (Pulse Intervals)**:
- Window size: 5 samples
- Purpose: Reject noise spikes in pulse timing
- Applied to: Pulse interval measurements (microseconds)
- Position: Before frequency calculation

**Moving Average Filter (RPM)**:
- Window size: 15 samples
- Sample rate: ~10 Hz (every pulse at typical RPM)
- Duration: ~1.5 seconds
- Purpose: Smooth RPM fluctuations for stable display
- Applied to: Calculated RPM values

**Rounding Filter**:
- Round to nearest 20 RPM before output
- Purpose: Reduce display jitter from minor fluctuations
- Example: 1987 RPM → 1980 RPM, 2003 RPM → 2000 RPM

## Data Models

### Pulse Timing Model

The system measures time intervals between consecutive rising edges:

```
Pulse 1 ──┐     ┌── Pulse 2 ──┐     ┌── Pulse 3
          └─────┘              └─────┘
          
          |<--- interval_1 --->|
                               |<--- interval_2 --->|
```

**Timestamp Capture**: Uses ESP32 `micros()` function (microsecond resolution)

**Interval Calculation**: `interval = current_timestamp - previous_timestamp`

**Frequency Derivation**: `frequency = 1,000,000 / interval_us`

### Ring Gear Model

The 116-tooth ring gear rotates with the engine crankshaft:

```
One Engine Revolution = 116 teeth pass sensor = 116 pulses
```

**Frequency-to-RPM Relationship**:
```
pulse_frequency = (RPM / 60) × teeth_per_revolution
pulse_frequency = (RPM / 60) × 116

Solving for RPM:
RPM = (pulse_frequency × 60) / 116
```

### Signal Conditioning Model

The magnetic pickup signal passes through conditioning stages:

```
Magnetic Pickup (AC signal, ~1-10V peak)
    ↓
Op-Amp (amplify and square wave conversion)
    ↓
Opto-Isolator (electrical isolation, 0-3.3V digital)
    ↓
ESP32 GPIO (digital input with interrupt)
```

**Signal Characteristics**:
- Input: Conditioned digital pulses (0-3.3V)
- Edge type: Rising edge detection (configurable)
- Pull-up/down: Internal pull-down enabled
- Interrupt mode: RISING edge trigger

## Correctness Properties

### Property Reflection

After analyzing all acceptance criteria, the following properties provide comprehensive, non-redundant validation:

### Property 1: Pulse frequency to RPM conversion correctness
*For any* valid pulse frequency between 1160 Hz and 7560 Hz, the calculated RPM should equal (frequency × 60) / 116 within ±0.1 RPM
**Validates: Requirements 1.3, 1.5, 4.4**

### Property 2: Interrupt response time
*For any* pulse edge detected on the GPIO pin, the ISR should execute and capture the timestamp within 10 microseconds
**Validates: Requirements 10.1, 10.2**

### Property 3: High frequency pulse handling
*For any* pulse frequency up to 10 kHz, the system should detect and count all pulses without missing any
**Validates: Requirements 2.5**

### Property 4: Signal loss detection
*For any* period where no pulses are received for 2 seconds, the output should transition to 0 within 2 seconds
**Validates: Requirements 3.1, 9.2, 9.3**

### Property 5: Low RPM threshold
*For any* calculated RPM below 600, the output should be 0 to indicate engine stopped
**Validates: Requirements 3.2, 9.1**

### Property 6: Maximum RPM validation
*For any* calculated RPM exceeding 4200, the output should be NaN to indicate invalid reading
**Validates: Requirements 3.3**

### Property 7: Startup behavior
*For any* system startup condition, the output should be 0 until at least 2 valid consecutive pulses are detected
**Validates: Requirements 3.5**

### Property 8: Calibration multiplier linearity
*For any* calibration multiplier value, the output RPM should equal the calculated RPM multiplied by exactly that factor
**Validates: Requirements 5.2**

### Property 9: Configuration parameter usage
*For any* configured teeth_per_revolution value, changing the configuration should change the calculated RPM for the same pulse frequency
**Validates: Requirements 5.1, 5.4**

### Property 10: Median filter noise rejection
*For any* sequence of pulse intervals where one value is an outlier (differs by >50% from median), the median filter output should not equal the outlier value
**Validates: Requirements 3.4, 7.1**

### Property 11: Moving average smoothing
*For any* sequence of RPM values with variance σ², the moving average output should have variance ≤ σ²/N where N is the window size
**Validates: Requirements 7.2**

### Property 12: RPM to Hz conversion
*For any* valid RPM value, the output to SignalK should equal RPM / 60 (revolutions per second)
**Validates: Requirements 1.4, 8.2**

### Property 13: Output throttling
*For any* sequence of RPM calculations, SignalK transmissions should occur no more frequently than once every 1.5 seconds
**Validates: Requirements 8.1**

### Property 14: NaN propagation
*For any* NaN RPM value, the system should transmit NaN to SignalK at 3-second intervals
**Validates: Requirements 8.3**

### Property 15: Rounding consistency
*For any* RPM value, the rounded output should be the nearest multiple of 20 RPM
**Validates: Requirements 8.4**

### Property 16: Accuracy at idle
*For any* pulse frequency corresponding to 600 RPM (1160 Hz), the output should be within ±10 RPM
**Validates: Requirements 4.1**

### Property 17: Accuracy at cruise
*For any* pulse frequency corresponding to 2000 RPM (3867 Hz), the output should be within ±5 RPM
**Validates: Requirements 4.2**

### Property 18: Accuracy at maximum
*For any* pulse frequency corresponding to 3600 RPM (6960 Hz), the output should be within ±10 RPM
**Validates: Requirements 4.3**

### Property 19: Non-blocking operation
*For any* pulse processing operation, the main loop should remain responsive and not block for more than 1 millisecond
**Validates: Requirements 6.4, 10.3, 10.4**

## Error Handling

### Signal Loss Conditions

1. **No Pulses Received**: If no pulse detected for 2 seconds, output 0 (engine stopped)
2. **First Pulse After Startup**: Ignore first pulse (no previous interval), wait for second pulse
3. **Timeout Detection**: Use watchdog timer to detect 2-second timeout

### Invalid Pulse Conditions

1. **Too Fast (<100 μs interval)**: Reject as noise, use previous valid interval
2. **Too Slow (>2 seconds interval)**: Treat as signal loss, output 0
3. **Erratic Timing**: Median filter rejects outliers automatically

### RPM Range Errors

1. **Below Idle (<600 RPM)**: Output 0 to indicate engine stopped
2. **Above Maximum (>4200 RPM)**: Output NaN to indicate invalid reading
3. **Negative RPM**: Should never occur, but if detected, output NaN

### Interrupt Safety

1. **ISR Execution Time**: Keep ISR under 10 μs to avoid blocking other interrupts
2. **Volatile Variables**: Use volatile for all ISR-to-main-thread communication
3. **Atomic Operations**: Use atomic flags for thread-safe state updates
4. **No Blocking in ISR**: No Serial.print(), no delays, no complex calculations

### Error Propagation

Error conditions result in specific outputs:
- Signal loss → 0 (engine stopped)
- Invalid high RPM → NaN
- Startup/no data → 0
- Noise/outliers → Filtered out, previous valid value maintained

## Testing Strategy

### Unit Testing Approach

Unit tests will verify specific examples and edge cases:

1. **RPM Calculation Tests**: Verify exact RPM for known frequencies (600, 2000, 3900 RPM)
2. **Frequency Conversion Tests**: Verify interval-to-frequency calculation accuracy
3. **Threshold Tests**: Verify 0 output below 600 RPM, NaN above 4200 RPM
4. **Timeout Tests**: Verify 0 output after 2-second signal loss
5. **Rounding Tests**: Verify 20 RPM rounding behavior

### Property-Based Testing Approach

Property-based tests will verify universal behaviors across all inputs using **fast-check**:

**Test Configuration**: Each property test will run a minimum of 100 iterations with randomly generated inputs.

**Generator Strategy**:
- **Pulse intervals**: 100 μs to 2,000,000 μs (10 kHz to 0.5 Hz)
- **Pulse frequencies**: 10 Hz to 10,000 Hz
- **RPM values**: 0 to 4500 RPM
- **Teeth counts**: 100 to 150 (typical ring gear range)
- **Calibration multipliers**: 0.9 to 1.1 (typical adjustment range)

**Property Test Tagging**: Each property-based test will include a comment tag:
```cpp
// **Feature: rpm-sender-interpreter, Property N: [property description]**
// **Validates: Requirements X.Y**
```

### Interrupt Testing

Interrupt behavior requires special testing approaches:

1. **Simulated Pulse Generation**: Use timer to generate test pulses at known frequencies
2. **ISR Execution Time**: Measure ISR duration using cycle counters
3. **Pulse Counting Accuracy**: Verify no pulses missed at high frequencies
4. **Thread Safety**: Verify volatile variable access is safe

### Integration Testing

Integration tests will verify the complete pipeline:

1. **End-to-End RPM Measurement**: Simulated pulses → RPM output
2. **Filter Chain**: Verify median → moving average → rounding sequence
3. **SignalK Integration**: Verify correct path and unit conversion
4. **Configuration Changes**: Verify parameter updates affect calculations
5. **Signal Loss Recovery**: Verify transition from running → stopped → running

### Hardware Testing

Hardware validation on actual ESP32:

1. **GPIO Interrupt Latency**: Measure actual ISR response time
2. **High Frequency Handling**: Test with signal generator up to 10 kHz
3. **Noise Immunity**: Test with noisy signals and electrical interference
4. **Long-term Stability**: Run for extended periods to verify no drift or overflow

### Test Data Sources

1. **Calculated Values**: Theoretical pulse frequencies for known RPM values
2. **Field Measurements**: Actual pulse timing from installed magnetic pickups
3. **Boundary Conditions**: Physical limits of engine RPM range
4. **Noise Patterns**: Realistic electrical noise profiles from marine environments

### Testing Tools

- **Unit Test Framework**: Google Test (C++) or PlatformIO native testing
- **Property-Based Testing**: fast-check (if using TypeScript simulation) or custom C++ generators
- **Hardware Testing**: Logic analyzer for pulse timing verification
- **Mocking**: Mock interrupt timing for deterministic unit tests
- **Coverage Target**: 90%+ code coverage, 100% of error paths

### Test Execution Strategy

1. Run unit tests first to catch basic functionality issues
2. Run property tests to verify universal behaviors
3. Run integration tests to verify pipeline operation
4. Run hardware tests on actual ESP32 for validation
5. Field testing on actual engine for final validation

The comprehensive testing approach ensures both specific examples work correctly (unit tests), general behaviors hold across all inputs (property tests), and the system performs reliably in real-world conditions (hardware and field tests).

## Implementation Considerations

### Memory Management

- **Static Variables**: Use static volatile variables for ISR communication
- **Circular Buffers**: Use fixed-size arrays for median and moving average filters
- **No Dynamic Allocation**: Avoid malloc/new in ISR or time-critical paths
- **Stack Usage**: Keep ISR stack usage minimal (<100 bytes)

### Timing Precision

- **Microsecond Resolution**: Use `micros()` for timestamp capture
- **Overflow Handling**: Handle `micros()` overflow every ~71 minutes
- **Jitter Compensation**: Median filter handles timing jitter from interrupts

### Power Efficiency

- **Interrupt-Driven**: No polling loops consuming CPU
- **Sleep Compatibility**: Design allows ESP32 light sleep between pulses
- **Minimal Processing**: Calculations only when new pulse arrives

### Debugging Support

- **Debug Outputs**: Optional Serial output of pulse intervals and RPM
- **SignalK Debug Paths**: Separate paths for raw frequency, filtered RPM, etc.
- **LED Indicators**: Optional LED blink on pulse detection
- **Statistics**: Track pulse count, missed pulses, ISR execution time

### Configuration Persistence

- **SensESP Config**: Store teeth count, calibration multiplier, GPIO pin
- **Factory Defaults**: Sensible defaults for 116-tooth ring gear
- **Runtime Updates**: Allow configuration changes without restart
- **Validation**: Validate configuration parameters on load

This design provides a robust, efficient, and accurate RPM measurement system suitable for marine diesel engine monitoring in harsh electrical environments.
