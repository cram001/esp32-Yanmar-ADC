# Requirements Document

## Introduction

The Engine Hours Tracker feature provides accurate recording and persistence of engine operating hours for marine diesel engines. The system monitors engine RPM to determine when the engine is running and accumulates operating time, storing it persistently across power cycles. This data is critical for maintenance scheduling, warranty tracking, and operational logging.

## Glossary

- **Engine Hours Tracker**: The software component responsible for monitoring engine runtime and accumulating operating hours
- **RPM**: Revolutions Per Minute, a measure of engine rotational speed
- **Operating Threshold**: The minimum RPM value (500 RPM) above which the engine is considered to be running
- **Persistence Layer**: The non-volatile storage mechanism (ESP32 Preferences) that retains hours data across power cycles
- **SensESP**: The Signal K sensor framework for ESP32 microcontrollers
- **Transform**: A SensESP component that receives input values, processes them, and emits output values
- **Config UI**: The web-based configuration interface provided by SensESP

## Requirements

### Requirement 1

**User Story:** As a boat operator, I want the system to automatically track engine operating hours, so that I can monitor total runtime without manual intervention.

#### Acceptance Criteria

1. WHEN the Engine Hours Tracker receives an RPM value above the Operating Threshold THEN the Engine Hours Tracker SHALL accumulate elapsed time to the total hours
2. WHEN the Engine Hours Tracker receives an RPM value at or below the Operating Threshold THEN the Engine Hours Tracker SHALL NOT accumulate time to the total hours
3. WHEN time is accumulated THEN the Engine Hours Tracker SHALL calculate elapsed time based on the difference between consecutive updates
4. WHEN the Engine Hours Tracker emits hours data THEN the Engine Hours Tracker SHALL round the value to one decimal place
5. WHEN the Engine Hours Tracker processes the first RPM reading after initialization THEN the Engine Hours Tracker SHALL NOT accumulate any time for that reading

### Requirement 2

**User Story:** As a boat operator, I want engine hours to persist across power cycles, so that I don't lose accumulated runtime data when the system restarts.

#### Acceptance Criteria

1. WHEN the Engine Hours Tracker initializes THEN the Engine Hours Tracker SHALL load the previously stored hours value from the Persistence Layer
2. WHEN the Engine Hours Tracker accumulates time THEN the Engine Hours Tracker SHALL save the updated hours value to the Persistence Layer at a rate of no less than every 30 seconds.
3. WHEN no previous hours data exists in the Persistence Layer THEN the Engine Hours Tracker SHALL initialize hours to zero
4. WHEN the Persistence Layer is unavailable THEN the Engine Hours Tracker SHALL handle the error gracefully and continue operating with in-memory values

### Requirement 3

**User Story:** As a boat operator, I want to view and manually adjust engine hours through the configuration interface, so that I can correct the value if needed or set it to match existing engine hour meters.

#### Acceptance Criteria

1. WHEN a user accesses the Config UI THEN the Engine Hours Tracker SHALL expose the current hours value as a configurable parameter
2. WHEN a user updates the hours value through the Config UI THEN the Engine Hours Tracker SHALL accept the new value and save it to the Persistence Layer
3. WHEN the hours value is updated through the Config UI THEN the Engine Hours Tracker SHALL emit the new value
4. WHEN the Config UI requests the schema THEN the Engine Hours Tracker SHALL provide a JSON schema defining the hours parameter as a number type

### Requirement 4

**User Story:** As a system integrator, I want the Engine Hours Tracker to integrate seamlessly with the SensESP framework, so that it can be easily connected to RPM sources and hour consumers.

#### Acceptance Criteria

1. WHEN the Engine Hours Tracker is instantiated THEN the Engine Hours Tracker SHALL inherit from the SensESP Transform base class with float input and float output
2. WHEN the Engine Hours Tracker receives RPM input THEN the Engine Hours Tracker SHALL process it through the Transform pipeline
3. WHEN the Engine Hours Tracker updates hours THEN the Engine Hours Tracker SHALL emit the new value through the Transform output
4. WHEN the Engine Hours Tracker is configured with a config path THEN the Engine Hours Tracker SHALL register itself with the SensESP configuration system

### Requirement 5

**User Story:** As a maintenance technician, I want accurate time accumulation regardless of update frequency, so that engine hours reflect actual operating time.

#### Acceptance Criteria

1. WHEN RPM updates arrive at varying intervals THEN the Engine Hours Tracker SHALL calculate elapsed time based on actual seconds elapsed between updates
2. WHEN calculating elapsed time THEN the Engine Hours Tracker SHALL convert seconds to hours 
3. WHEN the system time wraps around THEN the Engine Hours Tracker SHALL handle the overflow gracefully without losing accumulated hours
4. WHEN RPM updates are delayed THEN the Engine Hours Tracker SHALL accumulate the correct elapsed time based on the actual delay duration
5. Valid engine hours range is between 0.0 and 19999.9 hrs
