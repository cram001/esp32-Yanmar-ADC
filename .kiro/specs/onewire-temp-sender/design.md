# Design Document: OneWire Digital Temperature Sender Interpreter

## Overview

The OneWire Digital Temperature Sender Interpreter is a software component that reads temperature data from DS18B20 digital temperature sensors using the OneWire protocol and transmits the readings to a SignalK server. The system operates within the SensESP framework on an ESP32 microcontroller, managing multiple sensors on a single bus with independent configuration and error handling for each sensor.

The interpreter handles sensor discovery, address management, temperature conversion triggering, data reading with CRC validation, and graceful error handling. The design emphasizes reliability through retry logic, error isolation (one sensor failure doesn't affect others), and configurable resolution/timing parameters to balance accuracy with response time.

## Architecture

The system follows a sensor polling architecture with the following stages:

```
Initialization → Sensor Discovery → Configuration Mapping → 
Periodic Update Cycle → Conversion Trigger → Wait → Read All Sensors → 
Validate & Convert → Output to SignalK
```

### Component Flow

1. **Initialization Stage**: Discovers all DS18B20 sensors on the OneWire bus and logs their addresses
2. **Configuration Stage**: Maps discovered sensor addresses to SignalK paths based on user configuration
3. **Update Cycle**: Runs every 3 seconds to trigger temperature readings
4. **Conversion Trigger**: Sends simultaneous conversion command to all sensors on the bus
5. **Wait Period**: Waits for conversion to complete based on configured resolution (94ms to 750ms)
6. **Read Stage**: Reads temperature data from each configured sensor with CRC validation
7. **Validation**: Checks temperature is within valid range (-55°C to +125°C) and handles errors
8. **Output Stage**: Converts Celsius to Kelvin and emits to SignalK on unique paths per sensor

### Integration Points

- **SensESP Framework**: Inherits from `sensesp::Sensor` for periodic sensor reading
- **OneWire Library**: Uses OneWire/OneWireNg library for bus communication
- **DallasTemperature Library**: Uses DallasTemperature library for DS18B20 protocol handling
- **SignalK Protocol**: Outputs to configurable paths (e.g., `environment.inside.engineRoom.temperature`)
- **Configuration System**: Exposes sensor mappings and resolution through SensESP config UI

## Components and Interfaces

### OneWireTemperature Class

**Purpose**: Main sensor class that manages OneWire temperature sensors

**Interface**:
```cpp
class OneWireTemperature : public sensesp::Sensor {
public:
  OneWireTemperature(uint8_t pin, uint32_t update_interval = 3000);
  void start() override;
  void update() override;
  
  // Configuration
  void addSensor(const String& address, const String& sk_path);
  void setResolution(uint8_t bits);  // 9, 10, 11, or 12
  
private:
  OneWire* onewire_;
  DallasTemperature* sensors_;
  std::map<String, String> sensor_map_;  // address -> SK path
  uint8_t resolution_;
  uint32_t conversion_wait_ms_;
};
```

**Inputs**:
- OneWire bus pin number
- Update interval (default: 3000ms)
- Sensor address to SignalK path mappings

**Outputs**:
- Temperature in Kelvin for each configured sensor
- NaN for failed/invalid readings

**Configuration Parameters**:
- `pin`: GPIO pin for OneWire bus
- `resolution`: Temperature resolution (9-12 bits, default: 12)
- `update_interval`: Time between readings in milliseconds (default: 3000)
- `sensor_mappings`: Map of sensor addresses to SignalK paths

### SensorOutput Class

**Purpose**: Individual output stream for each sensor

**Interface**:
```cpp
class SensorOutput : public sensesp::FloatProducer {
public:
  SensorOutput(const String& sensor_address);
  void emit_value(float temperature);
  
private:
  String address_;
};
```

## Data Models

### Sensor Address Model

Each DS18B20 sensor has a unique 64-bit address:
- Family Code: 8 bits (0x28 for DS18B20)
- Serial Number: 48 bits (unique per device)
- CRC: 8 bits (for address validation)

Address format: `28:XX:XX:XX:XX:XX:XX:CRC`

### Temperature Resolution Model

DS18B20 supports four resolution settings:

| Resolution | Precision | Conversion Time |
|------------|-----------|-----------------|
| 9-bit      | 0.5°C     | 93.75ms        |
| 10-bit     | 0.25°C    | 187.5ms        |
| 11-bit     | 0.125°C   | 375ms          |
| 12-bit     | 0.0625°C  | 750ms          |

### Temperature Data Model

Raw temperature data from DS18B20:
- 16-bit signed integer in 1/16°C units
- Valid range: -55°C to +125°C
- Special values: 0x0550 (85°C) indicates power-on reset, not valid reading

### Configuration Model

Sensor configuration stored as JSON:
```json
{
  "pin": 4,
  "resolution": 12,
  "update_interval": 3000,
  "sensors": [
    {
      "address": "28:AA:BB:CC:DD:EE:FF:00",
      "sk_path": "environment.inside.engineRoom.temperature"
    },
    {
      "address": "28:11:22:33:44:55:66:77",
      "sk_path": "propulsion.mainEngine.exhaustTemperature"
    }
  ]
}
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property Reflection

After analyzing all acceptance criteria, the following redundancies were identified and eliminated:

- **Redundancy 1**: Requirements 1.5 and 8.1 both test Celsius to Kelvin conversion → Combined into Property 1
- **Redundancy 2**: Requirements 3.5 and 7.1 both test CRC retry logic → Combined into Property 8
- **Redundancy 3**: Requirements 1.3, 1.4, and 6.3 all test the update cycle behavior → Combined into Property 2
- **Redundancy 4**: Requirements 2.2, 2.3, and 8.4 all test path mapping → Combined into Property 4

The following properties provide comprehensive, non-redundant validation:

### Property 1: Temperature unit conversion
*For any* valid temperature in Celsius from a sensor, the output to SignalK should equal the Celsius value plus 273.15
**Validates: Requirements 1.5, 8.1**

### Property 2: Complete sensor reading cycle
*For any* update cycle, the system should trigger conversion on all sensors, wait for completion, and read from all configured sensors
**Validates: Requirements 1.3, 1.4, 6.3**

### Property 3: Sensor discovery completeness
*For any* set of sensors on the bus, the discovery process should find and log all sensors with their unique addresses
**Validates: Requirements 1.1, 1.2, 2.1**

### Property 4: Independent sensor path mapping
*For any* set of configured sensors, each sensor should map to a unique SignalK path, and readings should be emitted to the correct path
**Validates: Requirements 2.2, 2.3, 6.4, 8.4**

### Property 5: Error isolation
*For any* sensor that fails to read, only that sensor should output NaN while other sensors continue to provide valid readings
**Validates: Requirements 3.1, 3.4**

### Property 6: Invalid temperature rejection
*For any* temperature reading outside the valid range (-55°C to +125°C) or with invalid data, the output should be NaN
**Validates: Requirements 3.2, 4.3, 4.4**

### Property 7: Graceful degradation
*For any* configured sensor that is not found on the bus, the system should log a warning and continue operating with available sensors
**Validates: Requirements 2.4**

### Property 8: CRC error retry
*For any* sensor read that returns a CRC error, the system should retry exactly once before outputting NaN
**Validates: Requirements 3.5, 7.1**

### Property 9: Resolution configuration
*For any* valid resolution setting (9, 10, 11, or 12 bits), the sensor should be configured to that resolution and the conversion wait time should match the specification
**Validates: Requirements 5.1, 5.2**

### Property 10: Temperature precision
*For any* temperature reading at 12-bit resolution, the precision should be 0.0625°C or better
**Validates: Requirements 4.2**

### Property 11: Valid range accuracy
*For any* temperature between -55°C and +125°C, the system should output an accurate reading (within sensor specification of ±0.5°C)
**Validates: Requirements 4.1**

### Property 12: Update interval timing
*For any* configured update interval, sensor readings should occur at that interval (within ±10% tolerance for system timing)
**Validates: Requirements 5.3**

### Property 13: Output emission
*For any* successful temperature reading, the value should be emitted to connected transforms
**Validates: Requirements 6.2**

### Property 14: Consecutive failure handling
*For any* sensor with multiple consecutive read failures (3 or more), the system should log an error and continue outputting NaN
**Validates: Requirements 7.2**

### Property 15: NaN transmission skip
*For any* sensor reading that is NaN, the system should skip transmission to SignalK for that sensor
**Validates: Requirements 8.3**

## Error Handling

### Sensor Discovery Errors

1. **No Sensors Found**: Log error, continue operation, output NaN for all configured sensors
2. **Partial Discovery**: Log warning for missing configured sensors, operate with available sensors

### Read Errors

1. **CRC Error**: Retry read once, if still fails output NaN
2. **Timeout**: Output NaN, log warning
3. **Invalid Temperature**: Output NaN (e.g., 85°C power-on value, out-of-range)
4. **Bus Error**: Output NaN for all sensors, log error

### Configuration Errors

1. **Invalid Resolution**: Use default 12-bit, log warning
2. **Invalid Pin**: Fail initialization, log error
3. **Duplicate Paths**: Log warning, use first mapping
4. **Missing Sensor**: Log warning, skip that sensor

### Error Propagation

- Individual sensor errors are isolated - one sensor failure doesn't affect others
- NaN values are not transmitted to SignalK (transmission is skipped)
- Errors are logged for debugging but don't stop the update cycle
- System continues attempting to read sensors even after errors

## Testing Strategy

### Unit Testing Approach

Unit tests will verify specific examples and edge cases:

1. **Address Parsing Tests**: Verify correct parsing of 64-bit addresses
2. **Resolution Configuration Tests**: Verify each resolution setting (9, 10, 11, 12 bits)
3. **Edge Case Tests**: Verify NaN output for boundary conditions (no sensors, all sensors fail)
4. **Timing Tests**: Verify conversion wait times match resolution settings

### Property-Based Testing Approach

Property-based tests will verify universal behaviors across all inputs using **fast-check** (JavaScript/TypeScript PBT library):

**Test Configuration**: Each property test will run a minimum of 100 iterations with randomly generated inputs.

**Generator Strategy**:
- **Valid temperatures**: -55°C to +125°C
- **Invalid temperatures**: < -55°C, > +125°C, NaN, Infinity
- **Sensor counts**: 0 to 10 sensors
- **Resolutions**: 9, 10, 11, 12 bits
- **Update intervals**: 1000ms to 60000ms
- **Sensor addresses**: Valid 64-bit OneWire addresses

**Property Test Tagging**: Each property-based test will include a comment tag in the format:
```cpp
// **Feature: onewire-temp-sender, Property N: [property description]**
// **Validates: Requirements X.Y**
```

### Integration Testing

Integration tests will verify the complete system:

1. **Multi-Sensor Test**: Verify multiple sensors read independently
2. **Error Recovery Test**: Verify system recovers from sensor disconnection
3. **SignalK Integration**: Verify correct paths and unit conversion
4. **Configuration Changes**: Verify resolution changes take effect

### Test Data Sources

1. **DS18B20 Datasheet**: Temperature ranges, timing specifications
2. **OneWire Protocol Specification**: Address format, CRC algorithm
3. **Field Testing**: Actual sensor readings from marine installations

### Testing Tools

- **Unit Test Framework**: Google Test (C++) or Jest (if testing TypeScript port)
- **Property-Based Testing**: fast-check (JavaScript/TypeScript)
- **Mocking**: Mock OneWire bus for testing without hardware
- **Coverage Target**: 90%+ code coverage, 100% of error paths

### Test Execution Strategy

1. Run unit tests first to catch basic functionality issues
2. Run property tests to verify universal behaviors
3. Run integration tests to verify multi-sensor operation
4. Manual testing on hardware with real DS18B20 sensors

The dual testing approach ensures both specific examples work correctly (unit tests) and general behaviors hold across all inputs (property tests), providing comprehensive validation of the OneWire temperature interpreter.
