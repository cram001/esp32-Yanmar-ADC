# Implementation Plan

- [ ] 1. Review and refactor existing engine_load.h implementation
  - Review current implementation in src/engine_load.h against design document
  - Identify any gaps or deviations from requirements
  - Refactor code to match design specifications
  - Ensure proper error handling for all edge cases
  - Add inline documentation for formulas and constants
  - _Requirements: 1.1, 1.2, 2.1, 2.2, 2.3, 3.1, 3.2, 4.1, 4.2, 6.1, 6.2, 6.3, 6.4, 6.5_

- [ ]* 1.1 Write property test for load calculation formula
  - **Property 1: Load calculation formula and range invariant**
  - **Validates: Requirements 1.1, 1.2**

- [ ]* 1.2 Write property test for fuel-to-power conversion
  - **Property 2: Fuel-to-power conversion formula**
  - **Validates: Requirements 2.2, 2.3**

- [ ]* 1.3 Write property test for power curve interpolation
  - **Property 3: Power curve interpolation linearity**
  - **Validates: Requirements 3.2**

- [ ] 2. Implement RPM threshold validation
  - Add RPM threshold check (750 RPM minimum)
  - Return NaN when RPM is below threshold
  - Ensure RPM is read from Frequency transform correctly
  - Convert Hz to RPM for threshold comparison
  - _Requirements: 1.4, 4.3_

- [ ]* 2.1 Write unit test for RPM threshold edge case
  - Test RPM values below 750 return NaN
  - Test RPM values at exactly 750
  - Test RPM values above 750 return valid load
  - _Requirements: 1.4_

- [ ] 3. Enhance error handling and NaN propagation
  - Verify NaN propagation through calculation chain
  - Add checks for infinite values
  - Ensure division by zero protection
  - Test with mock transforms outputting NaN
  - _Requirements: 1.5, 4.5, 6.1, 6.2, 6.3, 6.4, 6.5_

- [ ]* 3.1 Write property test for NaN propagation
  - **Property 5: NaN propagation**
  - **Validates: Requirements 1.5, 4.5**

- [ ]* 3.2 Write unit tests for edge cases
  - Test zero fuel rate returns NaN
  - Test negative fuel rate returns NaN
  - Test max power = 0 returns NaN
  - Test max power < 0 returns NaN
  - Test infinite values return NaN
  - _Requirements: 2.5, 6.1, 6.2, 6.3, 6.4, 6.5_

- [ ] 4. Verify SignalK integration
  - Confirm SignalK output path is "propulsion.mainEngine.load"
  - Verify load values are transmitted as fractions (0.0 to 1.0)
  - Test NaN transmission to SignalK
  - Verify configuration path "/config/outputs/sk/engine_load"
  - _Requirements: 1.3, 7.1, 7.3_

- [ ]* 4.1 Write property test for SignalK output
  - **Property 6: SignalK output correctness**
  - **Validates: Requirements 1.3, 7.3**

- [ ]* 4.2 Write unit test for SignalK path configuration
  - Verify path is "propulsion.mainEngine.load"
  - _Requirements: 7.1_

- [ ] 5. Validate power curve calibration
  - Verify all 7 calibration points are correctly defined
  - Test exact RPM values return exact power values
  - Test interpolation between adjacent points
  - Test behavior at RPM below minimum curve point
  - Test behavior at RPM above maximum curve point
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [ ]* 5.1 Write property test for power curve lookup
  - **Property 7: Power curve lookup accuracy**
  - **Validates: Requirements 3.1**

- [ ]* 5.2 Write unit test for calibration points
  - Test each of the 7 calibration points returns exact power
  - Test (1800, 17.9), (2000, 20.9), (2400, 24.6), (2800, 26.8), (3200, 28.3), (3600, 29.5), (3800, 29.83)
  - _Requirements: 3.3_

- [ ]* 5.3 Write unit tests for out-of-range RPM
  - Test RPM below 1800 (minimum curve point)
  - Test RPM above 3800 (maximum curve point)
  - _Requirements: 3.4, 3.5_

- [ ] 6. Test transform integration
  - Create mock Frequency transform for RPM testing
  - Create mock fuel flow transform for testing
  - Verify load calculator reads from connected transforms
  - Test with various valid and invalid transform outputs
  - _Requirements: 4.1, 4.2, 4.3, 4.4_

- [ ]* 6.1 Write property test for transform integration
  - **Property 4: Transform integration**
  - **Validates: Requirements 4.3, 4.4**

- [ ] 7. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 8. Update main.cpp integration
  - Verify setup_engine_load() is called with correct parameters
  - Ensure g_frequency and g_fuel_lph are properly initialized before use
  - Verify no memory leaks or dangling pointers
  - Test full pipeline from sensor input to SignalK output
  - _Requirements: 4.1, 4.2_

- [ ]* 8.1 Write integration test for full pipeline
  - Test complete data flow from RPM/fuel inputs to SignalK output
  - Verify all transforms are properly connected
  - _Requirements: 1.1, 1.2, 1.3, 2.1, 3.1, 4.3, 4.4_

- [ ] 9. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.
