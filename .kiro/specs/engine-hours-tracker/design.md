# Design Document

## Overview

The Engine Hours Tracker is a SensESP Transform component that monitors engine RPM to accurately track and persist engine operating hours. It integrates seamlessly into the SensESP reactive framework, receiving RPM values as input and emitting accumulated hours as output. The component uses ESP32 Preferences for non-volatile storage and provides web-based configuration through the SensESP UI system.

The design prioritizes accuracy, reliability, and flash memory longevity by implementing time-based accumulation with configurable persistence intervals and proper handling of edge cases like system restarts and time wraparound.

## Architecture

### Component Hierarchy

```
Transform<float, float>
    └── EngineHours
```

The EngineHours class inherits from SensESP's Transform base class, making it a reactive component that:
- Receives float input (RPM values)
- Processes the input through time accumulation logic
- Emits float output (accumulated hours)
- Integrates with the SensESP configuration system

### Data Flow

```
RPM Source (Frequency Transform)
    ↓
EngineHours::set(rpm)
    ↓
[Time Accumulation Logic]
    ↓
[Persistence Check]
    ↓
EngineHours::emit(hours)
    ↓
Downstream Consumers (SK Output, Debug)
```

### Integration Points

1. **Input**: Connects to a Frequency transform that converts pulse counts to revolutions per second
2. **Output**: Connects to Linear transform (hours → seconds) then to SKOutputFloat for Signal K
3. **Storage**: ESP32 Preferences API with namespace "engine_runtime"
4. **Configuration**: SensESP ConfigItem system for web UI exposure

## Components and Interfaces

### EngineHours Class

**Inheritance**: `Transform<float, float>`

**Constructor**:
```cpp
EngineHours(const String& config_path = "")
```
- Initializes the Preferences namespace
- Loads persisted hours from flash
- Sets up configuration path for SensESP UI

**Public Methods**:

```cpp
void set(const float& rpm) override
```
- Receives RPM input from upstream transform
- Implements time accumulation logic
- Emits updated hours value
- Triggers persistence when needed

```cpp
bool to_json(JsonObject& json) override
```
- Serializes current hours value to JSON for configuration UI
- Returns true on success

```cpp
bool from_json(const JsonObject& json) override
```
- Deserializes hours value from JSON (user configuration)
- Saves updated value to flash
- Returns true on success

**Private Members**:

```cpp
float hours_
```
- Accumulated engine hours (in-memory)
- Range: 0.0 to 19999.9

```cpp
unsigned long last_update_
```
- Timestamp of last RPM update (milliseconds)
- Used for calculating elapsed time
- Initialized to 0 to prevent accumulation on first reading

```cpp
unsigned long last_save_
```
- Timestamp of last persistence operation (milliseconds)
- Used to enforce minimum 30-second save interval

```cpp
Preferences prefs_
```
- ESP32 Preferences handle for non-volatile storage
- Namespace: "engine_runtime"
- Key: "engine_hours"

**Private Methods**:

```cpp
void load_hours()
```
- Reads hours value from Preferences
- Defaults to 0.0 if no value exists
- Called during construction

```cpp
void save_hours()
```
- Writes current hours value to Preferences
- Implements rate limiting (30-second minimum interval)
- Handles persistence errors gracefully

### ConfigSchema Function

```cpp
String ConfigSchema(const EngineHours&)
```
- Returns JSON schema for SensESP configuration UI
- Defines hours parameter as editable number field
- Required for web-based configuration

## Data Models

### Hours Value
- **Type**: `float`
- **Unit**: Hours
- **Precision**: 0.1 hours (rounded for output)
- **Range**: 0.0 to 19999.9 hours
- **Storage**: 32-bit float in ESP32 Preferences

### Time Tracking
- **System Time**: `unsigned long` (milliseconds since boot)
- **Wraparound**: Handled at ~49.7 days (2^32 milliseconds)
- **Resolution**: Millisecond precision, converted to hours for accumulation

### RPM Threshold
- **Value**: 500.0 RPM
- **Type**: `float`
- **Purpose**: Distinguishes running engine from stopped/idle

## 
Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

Based on the requirements analysis, the following properties must hold for the Engine Hours Tracker:

### Property 1: RPM above threshold accumulates time
*For any* sequence of RPM values above 500.0 and their corresponding timestamps, the accumulated hours should equal the sum of time intervals between consecutive updates.
**Validates: Requirements 1.1, 1.3**

### Property 2: RPM at or below threshold does not accumulate time
*For any* sequence of RPM values at or below 500.0, the accumulated hours should remain unchanged from the initial value.
**Validates: Requirements 1.2**

### Property 3: Output rounding consistency
*For any* accumulated hours value, the emitted output should be rounded to exactly one decimal place.
**Validates: Requirements 1.4**

### Property 4: Persistence round-trip
*For any* valid hours value (0.0 to 19999.9), saving to persistence and then loading should return the same value within floating-point precision.
**Validates: Requirements 2.1**

### Property 5: Save rate limiting
*For any* sequence of accumulation events occurring within a 30-second window, at most one save operation should occur to the Persistence Layer.
**Validates: Requirements 2.2**

### Property 6: Configuration serialization round-trip
*For any* valid hours value, serializing to JSON via to_json() and deserializing via from_json() should preserve the value.
**Validates: Requirements 3.1, 3.2**

### Property 7: Configuration updates emit
*For any* valid hours value set through from_json(), the component should emit that value through the Transform output.
**Validates: Requirements 3.3**

### Property 8: Transform pipeline processing
*For any* RPM input value, calling set() should result in emit() being called with an updated hours value.
**Validates: Requirements 4.2**

### Property 9: Time calculation accuracy
*For any* sequence of timestamps with varying intervals, the accumulated time should equal the sum of (timestamp[i+1] - timestamp[i]) converted to hours.
**Validates: Requirements 5.1, 5.2**

### Property 10: Hours range constraint
*For any* hours value outside the range [0.0, 19999.9], the system should either clamp the value to the valid range or reject the input.
**Validates: Requirements 5.5**

## Error Handling

### Persistence Failures

**Strategy**: Graceful degradation
- If Preferences initialization fails, log error and continue with in-memory operation
- If save operations fail, log error but don't block accumulation
- If load operations fail, default to 0.0 hours and log warning

**Implementation**:
```cpp
void load_hours() {
    try {
        hours_ = prefs_.getFloat("engine_hours", 0.0f);
    } catch (...) {
        ESP_LOGW("EngineHours", "Failed to load hours, defaulting to 0.0");
        hours_ = 0.0f;
    }
}
```

### Time Wraparound

**Problem**: `millis()` wraps around after ~49.7 days (2^32 milliseconds)

**Strategy**: Detect wraparound and skip that update
```cpp
if (now < last_update_) {
    // Wraparound detected, skip this update
    last_update_ = now;
    return;
}
```

**Impact**: Loss of at most one update interval (~1 second) every 49.7 days

### Invalid RPM Values

**Strategy**: Defensive checks
- NaN values: Skip accumulation, don't emit
- Negative values: Treat as 0.0 (engine stopped)
- Extremely high values (>10000 RPM): Log warning but process normally

### Configuration Errors

**Strategy**: Validation and rejection
- Hours < 0.0: Clamp to 0.0
- Hours > 19999.9: Clamp to 19999.9
- Non-numeric JSON: Reject and log error, keep current value
- Missing JSON fields: Keep current value

## Testing Strategy

### Unit Testing

The Engine Hours Tracker requires both unit tests and property-based tests to ensure correctness:

**Unit Test Coverage**:
1. Initialization with no prior data (edge case from 2.3)
2. First RPM reading doesn't accumulate (edge case from 1.5)
3. Time wraparound handling (edge case from 5.3)
4. ConfigSchema returns valid JSON schema (example from 3.4)
5. Persistence error handling (error case from 2.4)

**Example Unit Tests**:
```cpp
TEST(EngineHours, InitializesWithZeroWhenNoPriorData) {
    // Clear preferences
    // Create EngineHours instance
    // Verify hours == 0.0
}

TEST(EngineHours, FirstReadingDoesNotAccumulate) {
    EngineHours tracker;
    tracker.set(1000.0f);  // First reading
    // Verify hours == 0.0
}

TEST(EngineHours, HandlesTimeWraparound) {
    // Simulate millis() wraparound
    // Verify hours don't decrease or accumulate incorrectly
}
```

### Property-Based Testing

**Framework**: We will use [Catch2 with Catch2's generators](https://github.com/catchorg/Catch2) for property-based testing in C++. Catch2 provides GENERATE() macros for creating randomized test inputs.

**Configuration**: Each property test should run a minimum of 100 iterations to ensure adequate coverage of the input space.

**Property Test Coverage**:

Each correctness property listed above will be implemented as a property-based test. The tests will:

1. **Generate random inputs** within valid ranges
2. **Execute the component** with those inputs
3. **Verify the property holds** for all generated inputs
4. **Report counterexamples** if the property fails

**Test Tagging**: Each property-based test must include a comment explicitly referencing the correctness property:
```cpp
// **Feature: engine-hours-tracker, Property 1: RPM above threshold accumulates time**
TEST_CASE("Property: RPM above threshold accumulates time", "[property][engine-hours]") {
    // Property test implementation
}
```

**Generator Strategies**:

1. **RPM values**: Generate floats in ranges [0, 500] and [500, 5000]
2. **Timestamps**: Generate increasing sequences with random intervals [100ms, 10000ms]
3. **Hours values**: Generate floats in range [0.0, 19999.9]
4. **Update sequences**: Generate arrays of (timestamp, rpm) pairs

**Smart Generators**:
- Ensure timestamps are monotonically increasing (except for wraparound tests)
- Generate RPM values clustered around the 500 RPM threshold to test boundary behavior
- Generate hours values near boundaries (0.0, 19999.9) to test clamping

### Integration Testing

**Scope**: Test EngineHours within the full SensESP pipeline

**Test Cases**:
1. Connect to Frequency transform and verify hours accumulate from pulse input
2. Connect to SKOutputFloat and verify Signal K messages are sent
3. Verify ConfigItem integration and web UI accessibility
4. Test persistence across simulated reboots

### Performance Testing

**Metrics**:
- Memory usage: Should remain constant (no leaks)
- CPU usage: set() should complete in <1ms
- Flash wear: Verify saves occur at most once per 30 seconds

**Test Approach**:
- Run for extended periods (hours) with varying RPM
- Monitor ESP32 heap and stack usage
- Count flash write operations

## Implementation Notes

### Floating-Point Precision

Hours are stored as 32-bit floats, providing:
- Precision: ~7 decimal digits
- At 19999.9 hours: Precision of ~0.001 hours (3.6 seconds)
- Sufficient for marine engine hour tracking

### Persistence Strategy

**Write Frequency**: Maximum once per 30 seconds
- Reduces flash wear (ESP32 flash rated for ~100,000 write cycles)
- At 30-second intervals: ~105,000 writes per year
- Flash lifetime: ~100 years at this rate

**Namespace**: "engine_runtime"
- Isolated from other SensESP preferences
- Allows independent clearing/reset

### SensESP Integration

**Transform Pattern**:
- Input: float (RPM from Frequency transform)
- Output: float (accumulated hours)
- Reactive: Automatically processes new RPM values

**Configuration Pattern**:
- to_json/from_json: Bidirectional serialization
- ConfigSchema: Defines UI schema
- ConfigItem: Registers with web UI

### Time Conversion

**Milliseconds to Hours**:
```
hours = milliseconds / 3,600,000
```

**Precision**: At 1ms resolution, precision is 0.000000278 hours (0.001 seconds)

### Rounding Strategy

**Output Rounding**:
```cpp
float rounded = float(int(hours_ * 10)) / 10.0f;
```
- Multiplies by 10, truncates to integer, divides by 10
- Result: Always one decimal place (e.g., 123.4, not 123.45)

## Dependencies

### External Libraries
- **SensESP**: Core framework (v3.1.1)
  - Transform base class
  - Configuration system
  - Event loop
- **ESP32 Preferences**: Non-volatile storage API
  - Part of ESP32 Arduino core
  - No additional installation required

### Internal Dependencies
- **Frequency Transform**: Provides RPM input
- **Linear Transform**: Converts hours to seconds for Signal K
- **SKOutputFloat**: Publishes to Signal K server

### Hardware Dependencies
- **ESP32 microcontroller**: Required for Preferences API
- **Flash memory**: Minimum 4MB recommended for SensESP applications

## Deployment Considerations

### Initial Setup
1. Component auto-initializes with 0.0 hours on first boot
2. User can manually set hours via web UI to match existing hour meter
3. Hours begin accumulating when RPM exceeds 500

### Maintenance
- No periodic maintenance required
- Hours can be reset via web UI if needed
- Preferences can be cleared via ESP32 erase_flash if necessary

### Monitoring
- Debug output available at `debug.engineHours` Signal K path
- Web UI shows current hours value
- Logs available via serial monitor for troubleshooting

### Backup and Recovery
- Hours stored in ESP32 flash (survives power loss)
- Can be exported via Signal K server
- Manual backup: Record value from web UI
- Recovery: Set value via web UI after flash erase

## Future Enhancements

### Potential Improvements
1. **Configurable RPM threshold**: Allow user to set threshold via UI
2. **Multiple accumulation modes**: Track hours at different RPM ranges
3. **Maintenance reminders**: Alert when hours reach service intervals
4. **Historical tracking**: Log hours per day/week/month
5. **Wear leveling**: Rotate between multiple flash keys to extend lifetime

### Backward Compatibility
- Current design uses fixed Preferences key "engine_hours"
- Future versions should maintain compatibility or provide migration
- Version field could be added to JSON schema for future changes
