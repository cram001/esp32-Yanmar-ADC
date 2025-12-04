# Requirements Document

## Introduction

This document specifies the requirements for an engine load calculator system for the Yanmar 3JH3E EPA marine diesel engine. The system calculates engine load as a percentage (0-100%) by comparing actual engine power output (derived from fuel consumption) against the maximum available power at the current RPM. This provides operators with real-time insight into how hard the engine is working, enabling better fuel efficiency management, engine health monitoring, and operational decision-making.

## Glossary

- **Engine Load**: The ratio of actual power output to maximum available power at current RPM, expressed as a fraction (0.0 to 1.0) or percentage (0% to 100%)
- **BSFC**: Brake Specific Fuel Consumption, measured in grams per kilowatt-hour (g/kWh), representing engine efficiency
- **Actual Power**: The power currently being produced by the engine, calculated from fuel consumption rate
- **Maximum Power**: The maximum power the engine can produce at the current RPM, based on manufacturer specifications
- **Fuel Rate**: The rate of fuel consumption measured in liters per hour (L/h)
- **RPM**: Revolutions Per Minute, the rotational speed of the engine crankshaft
- **SignalK**: An open-source marine data protocol for transmitting sensor data
- **Frequency Transform**: A SensESP component that converts pulse counts to revolutions per second (Hz)
- **Power Curve**: A lookup table mapping engine RPM to maximum available power output

## Requirements

### Requirement 1

**User Story:** As a marine engine operator, I want to see real-time engine load percentage, so that I can optimize fuel efficiency and avoid overloading the engine.

#### Acceptance Criteria

1. WHEN the engine is running THEN the Engine Load Calculator SHALL compute engine load as a fraction between 0.0 and 1.0
2. WHEN actual power and maximum power are available THEN the Engine Load Calculator SHALL calculate load as (actual power / maximum power)
3. WHEN engine load is calculated THEN the Engine Load Calculator SHALL output the load fraction to SignalK path "propulsion.mainEngine.load"
4. WHEN RPM is below idle threshold (750 RPM) THEN the Engine Load Calculator SHALL output NaN to indicate engine is not running
5. WHEN either actual power or maximum power is invalid THEN the Engine Load Calculator SHALL output NaN

### Requirement 2

**User Story:** As a system integrator, I want the load calculator to derive actual power from fuel consumption and RPM, so that I don't need additional torque or power sensors.

#### Acceptance Criteria

1. WHEN fuel consumption rate is available in liters per hour THEN the Engine Load Calculator SHALL convert fuel rate and RPM to power output
2. WHEN converting fuel and RPM to power THEN the Engine Load Calculator SHALL use the formula: Power (kW) = (fuel_kg/h × 1000) / BSFC
3. WHEN fuel rate is in liters per hour THEN the Engine Load Calculator SHALL convert to kg/h using diesel density of 0.84 kg/L
4. WHEN calculating power from fuel THEN the Engine Load Calculator SHALL use a BSFC value of 240 g/kWh
5. WHEN fuel rate is zero or negative THEN the Engine Load Calculator SHALL output NaN for actual power

### Requirement 3

**User Story:** As a marine engineer, I want maximum power calculated from the Yanmar 3JH3E power curve, so that load calculations reflect the engine's actual capabilities at each RPM.

#### Acceptance Criteria

1. WHEN RPM is provided THEN the Engine Load Calculator SHALL look up maximum power from the Yanmar 3JH3E power curve
2. WHEN RPM falls between curve points THEN the Engine Load Calculator SHALL interpolate maximum power linearly
3. WHEN the power curve is defined THEN the Engine Load Calculator SHALL include the following calibration points: (1800 RPM, 17.9 kW), (2000 RPM, 20.9 kW), (2400 RPM, 24.6 kW), (2800 RPM, 26.8 kW), (3200 RPM, 28.3 kW), (3600 RPM, 29.5 kW), (3800 RPM, 29.83 kW)
4. WHEN RPM is below the minimum curve point THEN the Engine Load Calculator SHALL extrapolate or output NaN
5. WHEN RPM exceeds the maximum curve point THEN the Engine Load Calculator SHALL use the maximum rated power value

### Requirement 4


**User Story:** As a system developer, I want the load calculator to integrate with existing RPM and fuel flow transforms, so that it can leverage already-processed sensor data.

#### Acceptance Criteria

1. WHEN the load calculator is initialized THEN the Engine Load Calculator SHALL accept a Frequency transform pointer as the RPM source
2. WHEN the load calculator is initialized THEN the Engine Load Calculator SHALL accept a fuel flow transform pointer (L/h) as the fuel rate source
3. WHEN RPM data is needed THEN the Engine Load Calculator SHALL read from the connected Frequency transform
4. WHEN fuel rate data is needed THEN the Engine Load Calculator SHALL read from the connected fuel flow transform
5. WHEN connected transforms output NaN THEN the Engine Load Calculator SHALL propagate NaN through the calculation chain

### Requirement 6

**User Story:** As a developer, I want the load calculator to handle invalid inputs gracefully, so that sensor failures don't crash the system or produce misleading data.

#### Acceptance Criteria

1. WHEN fuel rate is NaN THEN the Engine Load Calculator SHALL output NaN for actual power
2. WHEN maximum power is NaN or zero THEN the Engine Load Calculator SHALL output NaN for engine load
3. WHEN actual power is NaN THEN the Engine Load Calculator SHALL output NaN for engine load
4. WHEN maximum power is less than or equal to zero THEN the Engine Load Calculator SHALL output NaN to prevent division by zero
5. WHEN any calculation produces infinite values THEN the Engine Load Calculator SHALL output NaN

### Requirement 7



**User Story:** As a SignalK user, I want engine load data transmitted in the standard SignalK format, so that it's compatible with marine display systems and data loggers.

#### Acceptance Criteria

1. WHEN engine load is output to SignalK THEN the Engine Load Calculator SHALL use the path "propulsion.mainEngine.load"
2. WHEN engine load is transmitted THEN the Engine Load Calculator SHALL send values as a unitless fraction (0.0 to 1.0)
3. WHEN engine load is invalid THEN the Engine Load Calculator SHALL transmit NaN to SignalK
4. WHEN SignalK output is configured THEN the Engine Load Calculator SHALL provide a configurable SignalK output path
