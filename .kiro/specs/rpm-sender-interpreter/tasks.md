# Implementation Plan

- [ ] 1. Set up testing infrastructure
  - Create test directory structure for RPM sender interpreter tests
  - Configure fast-check or equivalent property-based testing library for C++
  - Set up test utilities for generating pulse timing data and simulating interrupts
  - Create mock interrupt system for deterministic testing
  - _Requirements: All (testing foundation)_

- [ ]* 1.1 Write property test for pulse frequency to RPM conversion
  - **Property 1: Pulse frequency to RPM conversion correctness**
  - **Validates: Requirements 1.3, 1.5, 4.4**

- [ ] 2. Implement core RPMSensor class structure
  - Create RPMSensor class inheriting from sensesp::Sensor
  - Implement constructor accepting pin, teeth_per_revolution, and calibration_multiplier parameters
  - Add private member variables for configuration and state
  - Declare static volatile variables for ISR communication (last_pulse_time_us_, pulse_interval_us_, new_pulse_available_)
  - _Requirements: 6.1, 10.2_

- [ ] 3. Implement interrupt service routine (ISR)
  - Create static IRAM_ATTR handleInterrupt() function
  - Capture current timestamp using micros()
  - Calculate pulse interval from previous timestamp
  - Update last_pulse_time_us_ and pulse_interval_us_
  - Set new_pulse_available_ flag
  - Ensure ISR executes in <10 microseconds
  - _Requirements: 1.1, 2.4, 10.1, 10.2_

- [ ]* 3.1 Write property test for interrupt response time
  - **Property 2: Interrupt response time**
  - **Validates: Requirements 10.1, 10.2**

- [ ] 4. Implement GPIO pin configuration and interrupt attachment
  - Configure GPIO pin as INPUT with pull-down
  - Attach interrupt using attachInterrupt() with RISING edge mode
  - Implement enable() method to start interrupt handling
  - Store pin number in configuration
  - _Requirements: 2.4, 5.3_

- [ ] 5. Implement pulse interval validation
  - Add check for interval < 100 μs (reject as noise, >10 kHz)
  - Add check for interval > 2,000,000 μs (2 seconds, signal loss)
  - Handle first pulse after startup (no previous interval available)
  - Reject invalid intervals and maintain previous valid value
  - _Requirements: 2.5, 3.1, 3.5_

- [ ]* 5.1 Write property test for high frequency pulse handling
  - **Property 3: High frequency pulse handling**
  - **Validates: Requirements 2.5**

- [ ] 6. Implement frequency calculation from pulse intervals
  - Calculate frequency using: frequency_hz = 1,000,000 / pulse_interval_us
  - Handle micros() overflow (every ~71 minutes)
  - Validate calculated frequency is within expected range
  - _Requirements: 1.2, 2.6_

- [ ] 7. Implement RPM conversion from frequency
  - Calculate RPM using: RPM = (frequency_hz × 60) / teeth_per_revolution
  - Apply calibration multiplier: RPM_final = RPM × calibration_multiplier
  - Use configured teeth_per_revolution value (default 116)
  - _Requirements: 1.3, 1.5, 4.4, 5.1, 5.2_

- [ ]* 7.1 Write property test for calibration multiplier linearity
  - **Property 8: Calibration multiplier linearity**
  - **Validates: Requirements 5.2**

- [ ]* 7.2 Write property test for configuration parameter usage
  - **Property 9: Configuration parameter usage**
  - **Validates: Requirements 5.1, 5.4**

- [ ] 8. Implement signal loss detection
  - Add timeout detection for no pulses received in 2 seconds
  - Output 0 when timeout occurs
  - Reset timeout timer on each valid pulse
  - Implement transition from running to stopped state
  - _Requirements: 3.1, 9.2, 9.3_

- [ ]* 8.1 Write property test for signal loss detection
  - **Property 4: Signal loss detection**
  - **Validates: Requirements 3.1, 9.2, 9.3**

- [ ] 9. Implement RPM range validation
  - Add check for RPM < 600, output 0 (engine stopped)
  - Add check for RPM > 4200, output NaN (invalid reading)
  - Add check for pulse frequency < 10 Hz, output 0
  - Implement startup behavior: output 0 until valid pulses detected
  - _Requirements: 3.2, 3.3, 3.5, 9.1_

- [ ]* 9.1 Write property test for low RPM threshold
  - **Property 5: Low RPM threshold**
  - **Validates: Requirements 3.2, 9.1**

- [ ]* 9.2 Write property test for maximum RPM validation
  - **Property 6: Maximum RPM validation**
  - **Validates: Requirements 3.3**

- [ ]* 9.3 Write property test for startup behavior
  - **Property 7: Startup behavior**
  - **Validates: Requirements 3.5**

- [ ] 10. Implement median filtering for pulse intervals
  - Create median filter with window size of 5 samples
  - Apply filter to pulse_interval_us values
  - Reject outliers that differ by >50% from median
  - Store filtered intervals in circular buffer
  - _Requirements: 3.4, 7.1_

- [ ]* 10.1 Write property test for median filter noise rejection
  - **Property 10: Median filter noise rejection**
  - **Validates: Requirements 3.4, 7.1**

- [ ] 11. Implement moving average filtering for RPM
  - Create moving average filter with window size of 15 samples
  - Apply filter to calculated RPM values
  - Smooth RPM fluctuations over ~1.5 seconds
  - Store RPM values in circular buffer
  - _Requirements: 7.2_

- [ ]* 11.1 Write property test for moving average smoothing
  - **Property 11: Moving average smoothing**
  - **Validates: Requirements 7.2**

- [ ] 12. Implement RPM rounding
  - Round RPM to nearest 20 RPM before output
  - Example: 1987 → 1980, 2003 → 2000
  - Apply rounding after moving average filter
  - _Requirements: 8.4_

- [ ]* 12.1 Write property test for rounding consistency
  - **Property 15: Rounding consistency**
  - **Validates: Requirements 8.4**

- [ ] 13. Implement SignalK output with unit conversion
  - Create SKOutputFloat for propulsion.mainEngine.revolutions path
  - Convert RPM to revolutions per second: Hz = RPM / 60
  - Configure 1.5-second throttle using event loop onRepeat
  - Handle NaN values: transmit NaN every 3 seconds
  - _Requirements: 1.4, 8.1, 8.2, 8.3_

- [ ]* 13.1 Write property test for RPM to Hz conversion
  - **Property 12: RPM to Hz conversion**
  - **Validates: Requirements 1.4, 8.2**

- [ ]* 13.2 Write property test for output throttling
  - **Property 13: Output throttling**
  - **Validates: Requirements 8.1**

- [ ]* 13.3 Write property test for NaN propagation
  - **Property 14: NaN propagation**
  - **Validates: Requirements 8.3**

- [ ] 14. Implement configuration support
  - Expose teeth_per_revolution parameter through SensESP config UI
  - Expose calibration_multiplier parameter through SensESP config UI
  - Expose GPIO pin parameter through SensESP config UI
  - Add ConfigItem for each parameter with validation
  - Set sensible defaults: 116 teeth, 1.0 multiplier, GPIO 34
  - _Requirements: 5.1, 5.2, 5.3, 5.4_

- [ ] 15. Implement non-blocking operation
  - Ensure main loop processing takes <1 ms per iteration
  - Perform all calculations outside ISR
  - Use flag-based communication between ISR and main loop
  - Minimize CPU usage when idle
  - _Requirements: 6.4, 10.3, 10.4_

- [ ]* 15.1 Write property test for non-blocking operation
  - **Property 19: Non-blocking operation**
  - **Validates: Requirements 6.4, 10.3, 10.4**

- [ ] 16. Implement accuracy validation
  - Verify accuracy at idle (600 RPM ± 10 RPM)
  - Verify accuracy at cruise (2000 RPM ± 5 RPM)
  - Verify accuracy at maximum (3600 RPM ± 10 RPM)
  - _Requirements: 4.1, 4.2, 4.3_

- [ ]* 16.1 Write property test for accuracy at idle
  - **Property 16: Accuracy at idle**
  - **Validates: Requirements 4.1**

- [ ]* 16.2 Write property test for accuracy at cruise
  - **Property 17: Accuracy at cruise**
  - **Validates: Requirements 4.2**

- [ ]* 16.3 Write property test for accuracy at maximum
  - **Property 18: Accuracy at maximum**
  - **Validates: Requirements 4.3**

- [ ] 17. Add debug output chain
  - Create debug SK outputs for pulse frequency, raw RPM, filtered RPM
  - Add optional Serial debug output for pulse intervals
  - Add pulse counter for diagnostics
  - Add ISR execution time measurement
  - _Requirements: Supporting debugging and troubleshooting_

- [ ]* 17.1 Write integration tests for complete pipeline
  - Test end-to-end pulse input to RPM output
  - Test filter chain operation (median → moving average → rounding)
  - Test SignalK integration with correct path and units
  - Test configuration parameter changes affect output
  - Test signal loss and recovery transitions
  - _Requirements: All (integration validation)_

- [ ] 18. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

