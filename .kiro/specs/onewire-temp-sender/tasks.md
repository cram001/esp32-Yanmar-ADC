# Implementation Plan

- [ ] 1. Set up testing infrastructure
  - Create test directory structure for OneWire temperature interpreter tests
  - Configure fast-check property-based testing library
  - Set up test utilities for generating valid test data (temperatures, sensor addresses, configurations)
  - Create mock OneWire bus for testing without hardware
  - _Requirements: All (testing foundation)_

- [ ]* 1.1 Write property test for temperature unit conversion
  - **Property 1: Temperature unit conversion**
  - **Validates: Requirements 1.5, 8.1**

- [ ] 2. Implement core OneWireTemperature class structure
  - Create OneWireTemperature class inheriting from sensesp::Sensor
  - Implement constructor accepting pin and update_interval parameters
  - Initialize OneWire and DallasTemperature library instances
  - Add private member variables for sensor mappings and configuration
  - _Requirements: 6.1_

- [ ] 3. Implement sensor discovery logic
  - Implement sensor discovery on OneWire bus during initialization
  - Read and store 64-bit address for each discovered sensor
  - Log each discovered sensor address
  - Handle case when no sensors are found
  - _Requirements: 1.1, 1.2, 2.1, 3.3_

- [ ]* 3.1 Write property test for sensor discovery completeness
  - **Property 3: Sensor discovery completeness**
  - **Validates: Requirements 1.1, 1.2, 2.1**

- [ ] 4. Implement sensor configuration and mapping
  - Implement addSensor() method to map sensor addresses to SignalK paths
  - Create separate output streams (SensorOutput) for each configured sensor
  - Validate that each sensor has a unique SignalK path
  - Handle configured sensors that are not found on the bus
  - _Requirements: 2.2, 2.3, 2.4, 6.4, 8.4_

- [ ]* 4.1 Write property test for independent sensor path mapping
  - **Property 4: Independent sensor path mapping**
  - **Validates: Requirements 2.2, 2.3, 6.4, 8.4**

- [ ]* 4.2 Write property test for graceful degradation
  - **Property 7: Graceful degradation**
  - **Validates: Requirements 2.4**

- [ ] 5. Implement resolution configuration
  - Implement setResolution() method for 9, 10, 11, or 12-bit resolution
  - Set conversion wait time based on resolution (94ms to 750ms)
  - Apply resolution setting to all sensors on the bus
  - Validate resolution parameter and use default if invalid
  - _Requirements: 5.1, 5.2, 5.4_

- [ ]* 5.1 Write property test for resolution configuration
  - **Property 9: Resolution configuration**
  - **Validates: Requirements 5.1, 5.2**

- [ ] 6. Implement update cycle and conversion trigger
  - Implement update() method that runs every 3 seconds
  - Trigger simultaneous temperature conversion on all sensors
  - Wait for conversion to complete based on configured resolution
  - Handle bus busy conditions
  - _Requirements: 1.3, 5.3, 7.3, 8.2_

- [ ]* 6.1 Write property test for complete sensor reading cycle
  - **Property 2: Complete sensor reading cycle**
  - **Validates: Requirements 1.3, 1.4, 6.3**

- [ ]* 6.2 Write property test for update interval timing
  - **Property 12: Update interval timing**
  - **Validates: Requirements 5.3**

- [ ] 7. Implement temperature reading with CRC validation
  - Read temperature from each configured sensor after conversion
  - Validate CRC for each reading
  - Implement retry logic for CRC errors (retry once)
  - Convert raw temperature data to Celsius
  - _Requirements: 1.4, 3.5, 7.1_

- [ ]* 7.1 Write property test for CRC error retry
  - **Property 8: CRC error retry**
  - **Validates: Requirements 3.5, 7.1**

- [ ] 8. Implement temperature validation and range checking
  - Validate temperature is within -55°C to +125°C range
  - Detect and reject invalid readings (e.g., 85°C power-on value)
  - Output NaN for out-of-range or invalid temperatures
  - Check temperature precision matches configured resolution
  - _Requirements: 3.2, 4.1, 4.2, 4.3, 4.4_

- [ ]* 8.1 Write property test for invalid temperature rejection
  - **Property 6: Invalid temperature rejection**
  - **Validates: Requirements 3.2, 4.3, 4.4**

- [ ]* 8.2 Write property test for valid range accuracy
  - **Property 11: Valid range accuracy**
  - **Validates: Requirements 4.1**

- [ ]* 8.3 Write property test for temperature precision
  - **Property 10: Temperature precision**
  - **Validates: Requirements 4.2**

- [ ] 9. Implement error isolation and handling
  - Ensure individual sensor failures only affect that sensor
  - Output NaN for failed sensor while others continue
  - Track consecutive failures per sensor
  - Log errors after multiple consecutive failures
  - _Requirements: 3.1, 3.4, 7.2_

- [ ]* 9.1 Write property test for error isolation
  - **Property 5: Error isolation**
  - **Validates: Requirements 3.1, 3.4**

- [ ]* 9.2 Write property test for consecutive failure handling
  - **Property 14: Consecutive failure handling**
  - **Validates: Requirements 7.2**

- [ ] 10. Implement output emission and SignalK integration
  - Convert Celsius to Kelvin for SignalK output
  - Emit temperature values to connected transforms
  - Skip transmission when temperature is NaN
  - Output to unique SignalK paths for each sensor
  - _Requirements: 1.5, 6.2, 8.1, 8.3_

- [ ]* 10.1 Write property test for output emission
  - **Property 13: Output emission**
  - **Validates: Requirements 6.2**

- [ ]* 10.2 Write property test for NaN transmission skip
  - **Property 15: NaN transmission skip**
  - **Validates: Requirements 8.3**

- [ ] 11. Add configuration UI integration
  - Expose pin configuration through SensESP config UI
  - Expose resolution configuration (9-12 bits)
  - Expose update interval configuration
  - Expose sensor address to SignalK path mappings
  - _Requirements: 5.4_

- [ ] 12. Add debug logging and diagnostics
  - Log sensor discovery results
  - Log configuration changes
  - Log read errors and retries
  - Log consecutive failure warnings
  - _Requirements: Supporting debugging and troubleshooting_

- [ ]* 12.1 Write integration tests for complete system
  - Test multi-sensor operation with independent readings
  - Test error recovery from sensor disconnection
  - Test SignalK integration with correct paths and units
  - Test configuration changes take effect
  - _Requirements: All (integration validation)_

- [ ] 13. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.
