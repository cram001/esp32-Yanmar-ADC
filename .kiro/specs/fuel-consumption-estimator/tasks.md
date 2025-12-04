# Implementation Plan

- [ ] 1. Create fuel curve data structures and validation
  - Create FuelCurvePoint struct to represent RPM-to-fuel-rate mappings
  - Implement validation logic to ensure curves have at least 2 points, ascending RPM values, and non-negative fuel rates
  - _Requirements: 1.2, 1.3, 1.4_

- [ ]* 1.1 Write property test for fuel curve validation
  - **Property 1: Fuel curve validation rejects invalid curves**
  - **Validates: Requirements 1.2, 1.3, 1.4**

- [ ] 2. Implement interpolation engine
  - Write linear interpolation function for calculating fuel rates between curve points
  - Implement binary search to efficiently find the relevant curve segment for a given RPM
  - Handle boundary conditions (RPM below minimum, above maximum, or exactly on a curve point)
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

- [ ]* 2.1 Write property test for interpolation correctness
  - **Property 2: Interpolation correctness across all RPM ranges**
  - **Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5**

- [ ] 3. Create FuelConsumptionEstimator transform class
  - Implement class extending SensESP Transform<float, float> for RPM input and fuel rate output
  - Add fuel curve storage member variable
  - Implement set_input() method to receive RPM and trigger calculation
  - Implement unit conversion from L/h to m³/s for SignalK output
  - _Requirements: 3.1, 4.1, 4.2_

- [ ]* 3.1 Write property test for SignalK output correctness
  - **Property 4: SignalK output reflects calculated values**
  - **Validates: Requirements 3.1**

- [ ] 4. Implement configuration system integration
  - Implement get_configuration() to serialize fuel curve to JSON
  - Implement set_configuration() to deserialize fuel curve from JSON
  - Add configuration schema for fuel curve array and fuel unit
  - Integrate with SensESP configuration persistence
  - _Requirements: 4.3, 4.4, 4.5_

- [ ]* 4.1 Write property test for configuration round-trip
  - **Property 3: Configuration round-trip preservation**
  - **Validates: Requirements 4.3**

- [ ] 5. Add error handling and default behavior
  - Implement error detection for invalid RPM input (NaN, negative values)
  - Publish null to SignalK when RPM is invalid
  - Use default fuel curve when configuration validation fails
  - Add logging for configuration errors
  - _Requirements: 1.5, 3.3_

- [ ]* 5.1 Write unit tests for error handling
  - Test invalid RPM input handling (example from requirement 3.3)
  - Test invalid configuration handling (example from requirement 1.5)
  - _Requirements: 1.5, 3.3_

- [ ] 6. Integrate with main application
  - Add FuelConsumptionEstimator to the SensESP pipeline in main.cpp
  - Connect RPM sensor output to fuel estimator input
  - Connect fuel estimator output to SignalK publisher
  - Configure default fuel curve with the specified values (500-3900 RPM range)
  - _Requirements: 4.1, 4.2_

- [ ]* 6.1 Write integration test for end-to-end data flow
  - Test RPM input flows through to SignalK output with correct unit conversion
  - Verify configuration persistence across restarts
  - _Requirements: 3.1, 4.4_

- [ ] 7. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.
