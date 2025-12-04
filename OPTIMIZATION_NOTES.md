# Code Optimization Analysis

## Flash Memory Savings Implemented

### 1. Conditional Debug Outputs (✅ DONE)
**Savings: ~5-10KB**

Added `ENABLE_DEBUG_OUTPUTS` flag in platformio.ini:
- Set to `0` to disable all 18 debug.* SignalK paths
- Set to `1` to enable for troubleshooting
- Affects: coolant (8 paths), RPM (3 paths), fuel (2 paths), engine hours (1 path), engine load (2 paths)

**Usage:**
```ini
-DENABLE_DEBUG_OUTPUTS=0  ; Production build
-DENABLE_DEBUG_OUTPUTS=1  ; Development/troubleshooting
```

## Additional Optimization Opportunities

### 2. Disable OTA Updates
**Potential Savings: ~300KB**

If you don't need over-the-air updates:
```cpp
// In setup(), comment out:
// ->enable_ota("esp32!")
```

Then use a non-OTA partition table to reclaim space.

### 3. Reduce Logging Level
**Potential Savings: ~10-20KB**

Current: `CORE_DEBUG_LEVEL=5` (verbose)
```ini
-D CORE_DEBUG_LEVEL=3  ; Warnings only (production)
-D CORE_DEBUG_LEVEL=5  ; Verbose (development)
```

### 4. Optimize String Storage
**Potential Savings: ~2-5KB**

Move repeated strings to PROGMEM:
```cpp
const char SK_PATH_COOLANT[] PROGMEM = "propulsion.mainEngine.coolantTemperature";
```

### 5. Reduce Config Path Strings
**Potential Savings: ~1-3KB**

Shorten config paths:
```cpp
// Current:
"/config/sensors/temperature/engine/linear"

// Optimized:
"/cfg/temp/eng/lin"
```

### 6. Remove Unused SensESP Features
**Potential Savings: ~20-50KB**

If not using certain features, add build flags:
```ini
-D SENSESP_DISABLE_SYSTEM_STATUS_LED=1  ; Already done
-D SENSESP_MINIMAL_BUILD=1              ; Disable unused features
```

### 7. Optimize Curve Interpolator Data
**Potential Savings: ~1KB**

Current coolant curve has 10 points. Could reduce to 6-7 key points:
```cpp
// Minimal curve (6 points vs 10)
{29.6f, 121.0f},
{85.5f, 85.0f},
{112.0f, 72.0f},
{207.0f, 56.0f},
{450.0f, 30.0f},
{1352.0f, 12.7f}
```

### 8. Combine Similar Transforms
**Potential Savings: ~2-4KB**

Merge multiple Linear(1.0, 0.0) identity transforms into single pass-through.

## Functional Improvements

### 1. Add Sensor Fault Detection
```cpp
// In coolant sender, detect:
- Open circuit (R > 2000Ω)
- Short circuit (R < 20Ω)
- Out of range voltage
// Emit NAN and set fault flag
```

### 2. Add Watchdog Timer
```cpp
// Prevent hangs
esp_task_wdt_init(30, true);  // 30 second timeout
```

### 3. Implement Exponential Moving Average
**Better than simple moving average for memory:**
```cpp
// Instead of MovingAverage(50) which stores 50 samples
// Use exponential smoothing (stores only 1 value)
float alpha = 0.1;  // smoothing factor
smoothed = alpha * new_value + (1 - alpha) * smoothed;
```

### 4. Add Temperature Rate-of-Change Alarm
```cpp
// Detect rapid temperature rise (coolant leak, etc)
if ((temp_C - last_temp) / dt > 5.0) {  // 5°C/sec
    // Trigger alarm
}
```

### 5. Optimize ADC Sampling Rate
Current: 10 Hz for coolant (slow-changing)
```cpp
// Coolant changes slowly, reduce to 1 Hz
static const float ADC_SAMPLE_RATE_HZ = 1.0f;  // Was 10.0f
// Saves CPU cycles and power
```

## Memory Usage Summary

| Feature | Flash Savings | Recommended |
|---------|--------------|-------------|
| Debug outputs OFF | 5-10 KB | Production: OFF, Dev: ON |
| OTA disabled | ~300 KB | If USB programming OK |
| Logging level 3 | 10-20 KB | Production: 3, Dev: 5 |
| Optimized curves | 1 KB | Optional |
| PROGMEM strings | 2-5 KB | If desperate |
| Exponential avg | 200 bytes | Better performance |

## Current Flash Usage
- 4MB unit: 92% used (with OTA + debug enabled)
- **With debug OFF**: ~88-90% used
- **With debug OFF + OTA OFF**: ~60-65% used

## Recommendations

**For 4MB Production Build:**
1. Set `ENABLE_DEBUG_OUTPUTS=0` ✅
2. Set `CORE_DEBUG_LEVEL=3`
3. Keep OTA enabled (useful for remote boats)
4. Monitor actual flash usage after build

**For 16MB Build:**
- Keep all debug outputs enabled
- Add more features (oil pressure, alarms, etc)
- No optimization needed

**For Development:**
- Keep `ENABLE_DEBUG_OUTPUTS=1`
- Keep `CORE_DEBUG_LEVEL=5`
- Use RPM simulator for bench testing
