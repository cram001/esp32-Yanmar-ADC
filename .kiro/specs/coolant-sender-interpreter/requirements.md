# Requirements Document

## Introduction

This document specifies the requirements for a Marine 450-30 Ohm (American D type) coolant sender interpreter system. The system converts analog resistance values from a marine engine coolant temperature sender into accurate digital temperature readings for transmission to a SignalK server. The interpreter must work in conjunction with an existing analog gauge without affecting gauge operation, handle voltage divider compensation, and provide reliable temperature measurements across the full operating range of marine diesel engines.

## Glossary

- **Coolant Sender**: A variable resistor sensor that changes resistance based on coolant temperature, ranging from 450 Ohms (cold) to 30 Ohms (hot) in American D type specification (actual range is greater than 450-30 Ohm)
- **ADC**: Analog-to-Digital Converter, the ESP32 component that converts analog voltage to digital values
- **Voltage Divider**: A passive circuit using two resistors to reduce voltage, specifically the DFRobot 30kΩ/7.5kΩ divider (5:1 ratio)
- **Gauge Coil Resistance**: The internal resistance of the analog coolant temperature gauge (approximately 1180Ω), to be confirmed on device
- **SignalK**: An open-source marine data protocol for transmitting sensor data
- **ESP32**: A microcontroller platform used for sensor data acquisition and processing
- **Sender Resistance**: The calculated resistance value of the coolant sender after compensating for voltage dividers and gauge circuit
- **Kelvin**: SI unit of temperature (K = °C + 273.15)

## Requirements

### Requirement 1

**User Story:** As a marine engine operator, I want to monitor coolant temperature digitally via SignalK, so that I can track engine health and receive alerts on multiple displays.

#### Acceptance Criteria

1. WHEN the system receives ADC voltage input THEN the Coolant Sender Interpreter SHALL convert the voltage to sender resistance
2. WHEN sender resistance is calculated THEN the Coolant Sender Interpreter SHALL map resistance to temperature using the American D type curve (user specified curve of the actual installed sensor)
3. WHEN temperature is determined THEN the Coolant Sender Interpreter SHALL output temperature in Kelvin to SignalK
4. WHEN the analog gauge is connected in series THEN the Coolant Sender Interpreter SHALL account for gauge coil resistance in calculations
5. WHEN voltage divider is present THEN the Coolant Sender Interpreter SHALL compensate for the divider ratio in voltage calculations

### Requirement 2

**User Story:** As a system integrator, I want the digital sensor to work alongside the existing analog gauge, so that I don't need to remove or modify the helm gauge installation.

#### Acceptance Criteria

1. WHEN the voltage divider taps the gauge circuit THEN the Coolant Sender Interpreter SHALL calculate sender resistance using series circuit analysis
2. WHEN gauge coil resistance is configured THEN the Coolant Sender Interpreter SHALL use the formula: Rsender = Rgauge × (Vtap / (Vsupply - Vtap))
3. WHEN supply voltage varies between 12V and 14.4V THEN the Coolant Sender Interpreter SHALL maintain accurate resistance calculations
4. WHEN the DFRobot 30kΩ/7.5kΩ voltage divider is used THEN the Coolant Sender Interpreter SHALL apply a 5.0× scaling factor to ADC voltage
5. If voltage divider input is accidentally shorted to 14V or 0V, no damage will occur to the ESP32.

### Requirement 3

**User Story:** As a marine electronics installer, I want the system to reject invalid sensor readings, so that faulty wiring or sensor failures don't produce misleading data.

#### Acceptance Criteria

1. WHEN ADC input is NaN or infinite THEN the Coolant Sender Interpreter SHALL output NaN
2. WHEN ADC voltage is below 0.01V THEN the Coolant Sender Interpreter SHALL output NaN to indicate no signal
3. WHEN calculated tap voltage exceeds supply voltage THEN the Coolant Sender Interpreter SHALL output NaN to indicate impossible condition
4. WHEN calculated sender resistance is below 20Ω THEN the Coolant Sender Interpreter SHALL output NaN to indicate short circuit
5. WHEN calculated sender resistance exceeds 1,350Ω THEN the Coolant Sender Interpreter SHALL output NaN to indicate open circuit

### Requirement 4

**User Story:** As a boat owner, I want the most accurate temperature readings near the normal operating range (70 to 85 deg C), outside of those temperature, variances of 5-10 deg C are acceptable.

#### Acceptance Criteria

1. WHEN sender resistance is 29.6Ω THEN the Coolant Sender Interpreter SHALL output 121°C
2. WHEN sender resistance is 1352Ω THEN the Coolant Sender Interpreter SHALL output 12.7°C
3. WHEN sender resistance falls between calibration points (hardcoded) THEN the Coolant Sender Interpreter SHALL interpolate temperature values
4. WHEN the provided (Calibrated for actual sensor) American D type curve is applied THEN the Coolant Sender Interpreter SHALL use the 10-point calibration table (29.6Ω to 1352Ω)
### Requirement 5

**User Story:** As a system maintainer, I want to easily calibrate and fine-tune the sensor readings (near normal operating temperatures), so that I can compensate for component tolerances and aging.

#### Acceptance Criteria

1. WHEN a calibration factor is configured THEN the Coolant Sender Interpreter SHALL apply linear scaling to sender resistance
2. WHEN supply voltage is configured THEN the Coolant Sender Interpreter SHALL use the configured value in resistance calculations
3. WHEN gauge coil resistance is configured THEN the Coolant Sender Interpreter SHALL use the configured value in resistance calculations
4. WHERE calibration is needed THEN the Coolant Sender Interpreter SHALL provide user-configurable parameters for voltage divider gain, supply voltage, and gauge resistance

### Requirement 6

**User Story:** As a developer, I want the interpreter to integrate with the SensESP framework, so that it can be part of a larger sensor processing pipeline.

#### Acceptance Criteria

1. WHEN the interpreter receives input THEN the Coolant Sender Interpreter SHALL inherit from sensesp::Transform<float, float>
2. WHEN valid resistance is calculated THEN the Coolant Sender Interpreter SHALL emit the result to connected transforms
3. WHEN invalid conditions are detected THEN the Coolant Sender Interpreter SHALL emit NaN to connected transforms
4. WHEN the set() method is called THEN the Coolant Sender Interpreter SHALL process the input and emit output synchronously

### Requirement 7

**User Story:** As a marine engineer, I want the system to handle ADC noise and electrical interference, so that temperature readings remain stable in the harsh marine electrical environment.

#### Acceptance Criteria

1. WHEN ADC readings contain noise THEN the system SHALL apply median filtering to reject spike noise
2. WHEN temperature values fluctuate THEN the system SHALL apply moving average filtering over 5 seconds

### Requirement 2

**User Story:** As a SignalK user, I want temperature updates at a reasonable rate, so that the system doesn't flood the network with excessive data and so tha the values are relatively steady for the user.

#### Acceptance Criteria 

1. WHEN temperature is calculated THEN the system SHALL throttle SignalK output to once every 10 seconds
2. WHEN temperature is output to SignalK THEN the system SHALL convert Celsius to Kelvin
3. WHEN temperature is NaN THEN the system SHALL transmit NAN to SignalK
