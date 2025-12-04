# Requirements Document

## Introduction

This feature provides fuel consumption estimation for marine engines based on RPM measurements and configurable fuel consumption curves. The system will interpolate fuel consumption rates from a pre-defined curve and integrate consumption over time to provide real-time fuel usage data for vessel monitoring.

## Glossary

- **FuelConsumptionEstimator**: The system component that calculates fuel consumption based on RPM input
- **FuelCurve**: A data structure containing RPM-to-fuel-rate mappings that define engine fuel consumption characteristics
- **FuelRate**: The instantaneous fuel consumption measured in liters per hour (L/h) or gallons per hour (GPH)
- **RPM**: Revolutions Per Minute, the rotational speed of the engine
- **InterpolationEngine**: The component responsible for calculating fuel rates between defined curve points
- **ConsumptionIntegrator**: The component that accumulates fuel consumption over time
- **SignalK**: The marine data protocol used for publishing sensor data

## Requirements

### Requirement 1

**User Story:** As a vessel operator, I want to configure a fuel consumption curve for my engine, so that the system can accurately estimate fuel usage based on RPM.

#### Acceptance Criteria

1. WHEN the system initializes, THE FuelConsumptionEstimator SHALL load a FuelCurve from configuration
2. WHERE a FuelCurve is defined, THE FuelCurve SHALL contain at least two RPM-to-FuelRate data points
3. WHEN a FuelCurve is provided, THE FuelConsumptionEstimator SHALL validate that RPM values are in ascending order
4. WHEN a FuelCurve is provided, THE FuelConsumptionEstimator SHALL validate that FuelRate values are non-negative
5. IF a FuelCurve fails validation, THEN THE FuelConsumptionEstimator SHALL report an error and use default values

### Requirement 2

**User Story:** As a vessel operator, I want the system to estimate fuel consumption at any RPM value, so that I can monitor fuel usage across the entire operating range.

#### Acceptance Criteria

1. WHEN an RPM value is received, THE InterpolationEngine SHALL calculate the corresponding FuelRate using the FuelCurve
2. WHEN the RPM value falls between two curve points, THE InterpolationEngine SHALL perform linear interpolation to determine the FuelRate
3. WHEN the RPM value is below the minimum curve point, THE InterpolationEngine SHALL return the FuelRate of the minimum curve point
4. WHEN the RPM value is above the maximum curve point, THE InterpolationEngine SHALL return the FuelRate of the maximum curve point
5. WHEN the RPM value equals a curve point, THE InterpolationEngine SHALL return the exact FuelRate for that point

### Requirement 3

**User Story:** As a vessel operator, I want fuel consumption rate published to SignalK, so that I can view it on my marine displays and integrate it with other vessel systems.

#### Acceptance Criteria

1. WHEN a FuelRate is calculated, THE FuelConsumptionEstimator SHALL publish the instantaneous rate to SignalK
2. WHEN publishing to SignalK, THE FuelConsumptionEstimator SHALL use standard SignalK paths for fuel data
3. WHEN RPM input is invalid or missing, THE FuelConsumptionEstimator SHALL publish null values to SignalK

### Requirement 4

**User Story:** As a developer, I want the fuel consumption estimator to integrate with the existing SensESP architecture, so that it works seamlessly with other sensors and transforms.

#### Acceptance Criteria

1. THE FuelConsumptionEstimator SHALL inherit from SensESP Transform classes to receive RPM input
2. WHEN RPM data is received, THE FuelConsumptionEstimator SHALL process it using the reactive programming pattern
3. THE FuelConsumptionEstimator SHALL provide configuration through SensESP configuration system
4. THE FuelConsumptionEstimator SHALL persist configuration to non-volatile storage
5. WHEN configuration changes, THE FuelConsumptionEstimator SHALL apply updates without requiring system restart
