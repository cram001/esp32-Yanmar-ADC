# Implementation Plan

- [ ] 1. Set up testing framework and project structure
  - Install Catch2 testing framework for C++ unit and property-based testing
  - Create test directory structure: `test/test_engine_hours.cpp`
  - Configure PlatformIO to build and run tests
  - Add test environment to `platformio.ini`
  - _Requirements: All (testing foundation)_

- [ ] 2. Implement core EngineHours class structure
  - [ ] 2.1 Create EngineHours class inheriting from Transform<float, float>
    - Define class in `src/engine_hours.h`
    - Implement constructor with config_path parameter
    - Initialize member variables (hours_, last_update_, last_save_)
    - Initialize Preferences with namespace "engine_runtime"
    - _Requirements: 1.1, 1.2, 4.1, 4.2_

  - [ ] 2.2 Implement load_hours() private method
    - Read "engine_hours" key from Preferences
    - Default to 0.0 if key doesn't exist
    - Add error handling for persistence failures
    - _Requirements: 2.1, 2.3, 2.4_

  - [ ] 2.3 Implement save_hours() private method with rate limiting
    - Check if 30 seconds have elapsed since last_save_
    - Write hours_ to Preferences only if interval met
    - Update last_save_ timestamp
    - Add error handling for write failures
    - _Requirements: 2.2, 2.4_

  - [ ]* 2.4 Write property test for persistence round-trip
    - **Property 4: Persistence round-trip**
    - **Validates: Requirements 2.1**

  - [ ]* 2.5 Write property test for save rate limiting
    - **Property 5: Save rate limiting**
    - **Validates: Requirements 2.2**

- [ ] 3. Implement time accumulation logic
  - [ ] 3.1 Implement set(const float& rpm) method
    - Get current timestamp using millis()
    - Check if last_update_ is non-zero (skip first reading)
    - Check if rpm > 500.0 (operating threshold)
    - Calculate elapsed time: (now - last_update_) / 3600000.0
    - Add elapsed time to hours_
    - Apply range clamping [0.0, 19999.9]
    - Update last_update_ to current time
    - Call save_hours() (rate limiting handled internally)
    - Round hours to one decimal place
    - Call emit() with rounded hours
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 5.1, 5.2, 5.5_

  - [ ] 3.2 Add time wraparound detection
    - Check if current timestamp < last_update_
    - If wraparound detected, update last_update_ but skip accumulation
    - Log warning about wraparound
    - _Requirements: 5.3_

  - [ ]* 3.3 Write property test for RPM above threshold accumulation
    - **Property 1: RPM above threshold accumulates time**
    - **Validates: Requirements 1.1, 1.3**

  - [ ]* 3.4 Write property test for RPM at/below threshold
    - **Property 2: RPM at or below threshold does not accumulate time**
    - **Validates: Requirements 1.2**

  - [ ]* 3.5 Write property test for output rounding
    - **Property 3: Output rounding consistency**
    - **Validates: Requirements 1.4**

  - [ ]* 3.6 Write property test for time calculation accuracy
    - **Property 9: Time calculation accuracy**
    - **Validates: Requirements 5.1, 5.2**

  - [ ]* 3.7 Write property test for hours range constraint
    - **Property 10: Hours range constraint**
    - **Validates: Requirements 5.5**

- [ ] 4. Implement configuration interface
  - [ ] 4.1 Implement to_json() method
    - Create JsonObject with "hours" field
    - Set value to current hours_
    - Return true on success
    - _Requirements: 3.1_

  - [ ] 4.2 Implement from_json() method
    - Check if "hours" field exists and is float
    - Validate range [0.0, 19999.9]
    - Clamp if out of range
    - Update hours_ member
    - Call save_hours() to persist
    - Call emit() to output new value
    - Return true on success
    - _Requirements: 3.2, 3.3, 5.5_

  - [ ] 4.3 Implement ConfigSchema() function
    - Return JSON schema string defining hours parameter
    - Schema should specify type: "number"
    - Include title: "Engine Hours"
    - _Requirements: 3.4_

  - [ ]* 4.4 Write property test for configuration serialization
    - **Property 6: Configuration serialization round-trip**
    - **Validates: Requirements 3.1, 3.2**

  - [ ]* 4.5 Write property test for configuration emit
    - **Property 7: Configuration updates emit**
    - **Validates: Requirements 3.3**

  - [ ]* 4.6 Write unit test for ConfigSchema format
    - Verify schema is valid JSON
    - Verify schema contains required fields
    - _Requirements: 3.4_

- [ ] 5. Add error handling and edge cases
  - [ ] 5.1 Add NaN and invalid RPM handling
    - Check for NaN RPM values, skip if detected
    - Check for negative RPM, treat as 0.0
    - Log warnings for invalid values
    - _Requirements: 1.1, 1.2_

  - [ ] 5.2 Enhance persistence error handling
    - Wrap Preferences calls in try-catch
    - Log errors but continue operation
    - Ensure in-memory hours_ always valid
    - _Requirements: 2.4_

  - [ ]* 5.3 Write unit test for first reading behavior
    - Verify first set() call doesn't accumulate
    - Verify last_update_ is set correctly
    - _Requirements: 1.5_

  - [ ]* 5.4 Write unit test for time wraparound handling
    - Simulate millis() wraparound scenario
    - Verify hours don't decrease
    - Verify accumulation resumes after wraparound
    - _Requirements: 5.3_

  - [ ]* 5.5 Write unit test for initialization with no prior data
    - Clear Preferences before test
    - Create EngineHours instance
    - Verify hours == 0.0
    - _Requirements: 2.3_

- [ ] 6. Integration and validation
  - [ ]* 6.1 Write property test for Transform pipeline
    - **Property 8: Transform pipeline processing**
    - **Validates: Requirements 4.2**

  - [ ]* 6.2 Write integration test with Frequency transform
    - Create mock Frequency transform
    - Connect to EngineHours
    - Send RPM values
    - Verify hours accumulate correctly
    - _Requirements: 4.2, 4.3_

  - [ ]* 6.3 Write integration test for full pipeline
    - Test EngineHours → Linear → SKOutputFloat chain
    - Verify hours converted to seconds correctly
    - Verify Signal K output format
    - _Requirements: 4.2, 4.3_

- [ ] 7. Documentation and code quality
  - [ ] 7.1 Add inline code documentation
    - Document all public methods with Doxygen comments
    - Document private methods and member variables
    - Add usage examples in header comments
    - _Requirements: All_

  - [ ] 7.2 Add logging statements
    - Log initialization with loaded hours value
    - Log persistence operations (saves)
    - Log errors and warnings
    - Use ESP_LOG macros with appropriate levels
    - _Requirements: 2.1, 2.2, 2.4_

  - [ ] 7.3 Code review and cleanup
    - Verify const correctness
    - Check for memory leaks
    - Ensure consistent naming conventions
    - Remove any debug code
    - _Requirements: All_

- [ ] 8. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.
