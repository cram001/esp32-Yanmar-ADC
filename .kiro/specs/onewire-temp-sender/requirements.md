# Requirements Document

## Introduction

This document specifies the requirements for a OneWire digital temperature sender interpreter system for marine applications. The system reads temperature data from OneWire digital temperature sensors (DS18B20 or compatible) and transmits accurate temperature readings to a SignalK server. The interpreter must handle multiple sensors on a single bus, provide reliable measurements across the marine operating temperature range, and integrate seamlessly with the SensESP framework.

## Glossary

- **OneWire**: A serial communication protocol using a single data line for communication, commonly used for DS18B20 temperature sensors
- **DS18B20**: A popular digital temperature sensor using the OneWire protocol, providing temperature readings from -55°C to +125°C
- **Sensor Address**: A unique 64-bit identifier for each OneWire device on the bus
- **SignalK**: An open-source marine data protocol for transmitting sensor data
- **ESP32**: A microcontroller platform used for sensor data acquisition and processing
- **SensESP**: A framework for building sensor applications on ESP32 that integrate with SignalK
- **Temperature Resolution**: The precision of temperature measurements, configurable from 9-bit (0.5°C) to 12-bit (0.0625°C)
- **Conversion Time**: The time required for the sensor to perform a temperature measurement, ranging from 94ms (9-bit) to 750ms (12-bit)

## Requirements

### Requirement 1

**User Story:** As a marine engine operator, I want to monitor temperatures from digital OneWire sensors via SignalK, so that I can track multiple temperature points with high accuracy.

#### Acceptance Criteria

1. WHEN the system initializes THEN the OneWire Temperature Interpreter SHALL discover all connected DS18B20 sensors on the bus
2. WHEN a sensor is discovered THEN the OneWire Temperature Interpreter SHALL read the unique 64-bit address
3. WHEN temperature is requested THEN the OneWire Temperature Interpreter SHALL trigger conversion on all sensors simultaneously
4. WHEN conversion completes THEN the OneWire Temperature Interpreter SHALL read temperature from each sensor
5. WHEN temperature is read THEN the OneWire Temperature Interpreter SHALL output temperature in Kelvin to SignalK

### Requirement 2

**User Story:** As a system integrator, I want to configure which sensors map to which SignalK paths, so that I can assign meaningful names to temperature readings.

#### Acceptance Criteria

1. WHEN sensors are discovered THEN the OneWire Temperature Interpreter SHALL log each sensor address for configuration
2. WHEN a sensor address is configured THEN the OneWire Temperature Interpreter SHALL map that sensor to a specific SignalK path
3. WHEN multiple sensors are present THEN the OneWire Temperature Interpreter SHALL support independent path configuration for each sensor
4. WHEN a configured sensor is not found THEN the OneWire Temperature Interpreter SHALL log a warning but continue operating with available sensors
5. WHEN the device reboots, previous associations between specific senor to SignalK path is retained.

### Requirement 3

**User Story:** As a marine electronics installer, I want the system to handle sensor failures gracefully, so that one faulty sensor doesn't affect other readings.

#### Acceptance Criteria

1. WHEN a sensor read fails THEN the OneWire Temperature Interpreter SHALL output NaN for that sensor only
2. WHEN a sensor returns an invalid temperature THEN the OneWire Temperature Interpreter SHALL output NaN
3. WHEN no sensors are found on the bus THEN the OneWire Temperature Interpreter SHALL log an error and output NaN
4. WHEN a sensor is disconnected during operation THEN the OneWire Temperature Interpreter SHALL detect the failure and output NaN
5. WHEN a sensor returns a CRC error THEN the OneWire Temperature Interpreter SHALL retry the read once before outputting NaN

### Requirement 4

**User Story:** As a boat owner, I want accurate temperature readings across the full marine operating range, so that I can monitor engine and environmental temperatures reliably.

#### Acceptance Criteria

1. WHEN temperature is between -55°C and +125°C THEN the OneWire Temperature Interpreter SHALL output accurate readings
2. WHEN 12-bit resolution is configured THEN the OneWire Temperature Interpreter SHALL provide precision of 0.0625°C
3. WHEN temperature is read THEN the OneWire Temperature Interpreter SHALL validate the reading is within sensor specifications
4. WHEN temperature exceeds +125°C THEN the OneWire Temperature Interpreter SHALL output NaN to indicate out-of-range



### Requirement 5

**User Story:** As a developer, I want the interpreter to integrate with the SensESP framework, so that it can be part of a larger sensor processing pipeline.

#### Acceptance Criteria

1. WHEN the interpreter is instantiated THEN the OneWire Temperature Interpreter SHALL inherit from sensesp::Sensor
2. WHEN temperature is read THEN the OneWire Temperature Interpreter SHALL emit the result to connected transforms
3. WHEN the update cycle runs THEN the OneWire Temperature Interpreter SHALL trigger conversion and read all configured sensors
4. WHEN sensors are configured THEN the OneWire Temperature Interpreter SHALL create separate output streams for each sensor

### Requirement 6

**User Story:** As a marine engineer, I want the system to handle electrical noise on the OneWire bus, so that temperature readings remain reliable in the harsh marine electrical environment.

#### Acceptance Criteria

1. WHEN a CRC error is detected THEN the OneWire Temperature Interpreter SHALL retry the read operation
2. WHEN multiple consecutive read failures occur THEN the OneWire Temperature Interpreter SHALL log an error and output NaN
3. WHEN the bus is busy THEN the OneWire Temperature Interpreter SHALL wait for bus availability before initiating conversion

### Requirement 8

**User Story:** As a SignalK user, I want temperature updates every 3 seconds, so that I have timely data without excessive network load.

#### Acceptance Criteria

1. WHEN temperature is read THEN the OneWire Temperature Interpreter SHALL convert Celsius to Kelvin
2. WHEN the update cycle runs THEN the OneWire Temperature Interpreter SHALL read sensors every 3 seconds
3. WHEN temperature is NaN THEN the OneWire Temperature Interpreter SHALL skip transmission to SignalK for that sensor
4. WHEN sensors are configured THEN the OneWire Temperature Interpreter SHALL output to unique SignalK paths for each sensor
