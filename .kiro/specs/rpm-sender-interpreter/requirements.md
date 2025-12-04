# Requirements Document

## Introduction

This document specifies the requirements for a Marine Engine RPM Interpreter system. The system converts digital pulse signals from a magnetic pickup sensor mounted on the engine's ring gear into accurate RPM readings for transmission to a SignalK server. The magnetic pickup detects teeth on a 116-tooth ring gear, and the signal is conditioned through an op-amp and opto-isolator before reaching the ESP32 digital input pin. The interpreter must accurately calculate RPM from pulse frequency, handle noise and signal conditioning, and provide reliable RPM measurements across the full operating range of marine diesel engines.

## Glossary

- **Magnetic Pickup**: A sensor that generates electrical pulses when ferrous metal (gear teeth) passes near it
- **Ring Gear**: A toothed gear mounted on the engine flywheel or crankshaft with 116 teeth
- **Op-Amp**: Operational amplifier used to condition and amplify the magnetic pickup signal
- **Opto-Isolator**: An optical isolation component that electrically isolates the sensor circuit from the ESP32 to protect against voltage spikes
- **Digital Input Pin**: An ESP32 GPIO pin configured to detect rising or falling edges of digital pulses
- **Pulse Frequency**: The rate at which gear teeth pass the magnetic pickup, measured in Hz
- **SignalK**: An open-source marine data protocol for transmitting sensor data
- **ESP32**: A microcontroller platform used for sensor data acquisition and processing
- **Teeth Per Revolution**: The number of gear teeth (116) that pass the sensor during one complete engine revolution
- **Interrupt Service Routine (ISR)**: A function that executes immediately when a digital pulse edge is detected

## Requirements

### Requirement 1

**User Story:** As a marine engine operator, I want to monitor engine RPM digitally via SignalK, so that I can track engine performance and receive alerts on multiple displays.

#### Acceptance Criteria

1. WHEN the system receives digital pulses from the magnetic pickup THEN the RPM Interpreter SHALL count the pulses using interrupt-driven edge detection
2. WHEN pulses are counted THEN the RPM Interpreter SHALL calculate pulse frequency in Hz
3. WHEN pulse frequency is determined THEN the RPM Interpreter SHALL convert frequency to RPM using the formula: RPM = (frequency × 60) / teeth_per_revolution
4. WHEN RPM is calculated THEN the RPM Interpreter SHALL output RPM as revolutions per second (Hz) to SignalK
5. WHEN the 116-tooth ring gear configuration is used THEN the RPM Interpreter SHALL use 116 as the teeth_per_revolution value

### Requirement 2

**User Story:** As a system integrator, I want accurate pulse counting across the full RPM range, so that the system works reliably from idle to maximum engine speed.

#### Acceptance Criteria

1. WHEN the engine operates at idle (600 RPM) THEN the RPM Interpreter SHALL detect pulses at approximately 1160 Hz
2. WHEN the engine operates at maximum speed (3900 RPM) THEN the RPM Interpreter SHALL detect pulses at approximately 6960 Hz
3. WHEN pulse frequency changes THEN the RPM Interpreter SHALL update the RPM calculation within 1 second
4. WHEN the ESP32 digital input pin receives a pulse THEN the RPM Interpreter SHALL trigger an interrupt service routine
5. WHEN pulses arrive at high frequency THEN the RPM Interpreter SHALL handle pulse rates up to 10 kHz without missing counts
6. WHEN measuring pulse frequency THEN the RPM Interpreter SHALL calculate frequency by measuring the time interval between consecutive pulses rather than counting pulses over a fixed time window

### Requirement 3

**User Story:** As a marine electronics installer, I want the system to reject invalid readings and handle signal loss, so that faulty wiring or sensor failures don't produce misleading data.

#### Acceptance Criteria

1. WHEN no pulses are received for 2 seconds THEN the RPM Interpreter SHALL output 0 to indicate engine stopped
2. WHEN pulse frequency is below 10 Hz THEN the RPM Interpreter SHALL output 0 to indicate engine stopped
3. WHEN calculated RPM exceeds 4200 THEN the RPM Interpreter SHALL output NaN to indicate invalid reading
4. WHEN pulse timing is erratic (variance > 5%) THEN the RPM Interpreter SHALL apply filtering to reject noise
5. WHEN the system starts up THEN the RPM Interpreter SHALL output 0 until valid pulses are detected

### Requirement 4

**User Story:** As a boat owner, I want accurate RPM readings across the engine's operating range from idle (600 RPM) to maximum (4000 RPM), so that I can monitor engine performance reliably.

#### Acceptance Criteria

1. WHEN the engine operates at idle (600 RPM) THEN the RPM Interpreter SHALL output RPM within ±10 RPM
2. WHEN the engine operates at cruise speed (2000 RPM) THEN the RPM Interpreter SHALL output RPM within ±5 RPM
3. WHEN the engine operates at maximum speed (3600 RPM) THEN the RPM Interpreter SHALL output RPM within ±10 RPM
4. WHEN RPM is calculated THEN the RPM Interpreter SHALL use the exact formula: RPM = (pulse_frequency_hz × 60) / 116

### Requirement 5

**User Story:** As a system maintainer, I want to easily configure the RPM readings, so that I can compensate for different ring gear tooth counts and sensor configurations.

#### Acceptance Criteria

1. WHEN teeth per revolution is configured THEN the RPM Interpreter SHALL use the configured value in RPM calculations
2. WHEN a calibration multiplier is configured THEN the RPM Interpreter SHALL apply the multiplier to the calculated RPM
3. WHEN the digital input pin is configured THEN the RPM Interpreter SHALL use the specified GPIO pin for pulse detection
4. WHERE configuration is needed THEN the RPM Interpreter SHALL provide user-configurable parameters for teeth count, calibration multiplier, and GPIO pin assignment

### Requirement 6

**User Story:** As a developer, I want the interpreter to integrate with the SensESP framework, so that it can be part of a larger sensor processing pipeline.

#### Acceptance Criteria

1. WHEN the interpreter processes pulses THEN the RPM Interpreter SHALL use SensESP sensor framework for integration
2. WHEN valid RPM is calculated THEN the RPM Interpreter SHALL emit the result to SignalK output
3. WHEN invalid conditions are detected THEN the RPM Interpreter SHALL emit NaN or 0 as appropriate
4. WHEN pulse interrupts occur THEN the RPM Interpreter SHALL handle them efficiently without blocking other SensESP operations

### Requirement 7

**User Story:** As a marine engineer, I want the system to handle electrical noise and interference, so that RPM readings remain stable in the harsh marine electrical environment.

#### Acceptance Criteria

1. WHEN pulse timing varies due to noise THEN the system SHALL apply median filtering to pulse intervals to reject outliers
2. WHEN RPM values fluctuate THEN the system SHALL apply moving average filtering over 1.5 seconds to smooth the output

### Requirement 8

**User Story:** As a SignalK user, I want RPM updates at a reasonable rate, so that the system doesn't flood the network with excessive data while maintaining responsive readings.

#### Acceptance Criteria

1. WHEN RPM is calculated THEN the system SHALL throttle SignalK output to once every 1.5 second
2. WHEN RPM is output to SignalK THEN the system SHALL convert RPM to revolutions per second (Hz) by dividing by 60
3. WHEN RPM is NaN THEN the system SHALL send NAN  to SignalK every 3 seconds
4. WHEN RPM is calculated THEN the system SHALL round the value to the nearest 20 RPM before conversion to Hz to avoid frequent display changes

### Requirement 9

**User Story:** As a boat owner, I want the system to detect when the engine is not running, so that the display shows zero RPM instead of invalid readings.

#### Acceptance Criteria

1. WHEN calculated RPM is below 600 THEN the RPM Interpreter SHALL output 0 to indicate engine stopped
2. WHEN no pulses are received for 2 seconds THEN the RPM Interpreter SHALL output 0 to indicate engine stopped
3. WHEN the engine transitions from running to stopped THEN the RPM Interpreter SHALL update the output to 0 within 2 seconds

### Requirement 10

**User Story:** As a developer, I want the pulse counting to be efficient and non-blocking, so that it doesn't interfere with other system operations.

#### Acceptance Criteria

1. WHEN a pulse edge is detected THEN the RPM Interpreter SHALL execute the interrupt service routine in less than 10 microseconds
2. WHEN pulse counting occurs THEN the RPM Interpreter SHALL use hardware interrupts rather than polling
3. WHEN RPM calculation occurs THEN the RPM Interpreter SHALL perform calculations outside of the interrupt service routine
4. WHEN the system is idle THEN the RPM Interpreter SHALL consume minimal CPU resources
