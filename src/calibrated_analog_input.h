#pragma once

#include "sensesp/sensors/sensor.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp_app_builder.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_log.h>

using namespace sensesp;

// ============================================================================
// CalibratedAnalogInput — SensESP v3.1.1 compatible
//
// • Uses ESP32 ADC1 directly (bypasses Arduino analogRead)
// • Applies Espressif ADC calibration if present (Two-Point or eFuse Vref)
// • Falls back to internal reference (1100 mV) if no eFuse data
// • Emits calibrated ADC-pin voltage (Volts)
// • Publishes calibration mode to Signal K (debug, static)
// ============================================================================

class CalibratedAnalogInput : public Sensor<float> {
 public:
  CalibratedAnalogInput(int pin,
                        float read_rate_hz,
                        const String& config_path = "")
      : Sensor<float>(config_path),
        pin_(pin),
        read_rate_hz_(read_rate_hz) {

    channel_ = adc1_channel_for_pin(pin_);

    // -----------------------------------------------------------------------
    // Configure ESP32 ADC hardware
    // -----------------------------------------------------------------------
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel_, ADC_ATTEN_DB_11);

    // -----------------------------------------------------------------------
    // Characterize ADC (factory calibration if present)
    // -----------------------------------------------------------------------
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100,   // fallback Vref (used only if no eFuse data)
        &adc_chars_);

    switch (cal_type) {
      case ESP_ADC_CAL_VAL_EFUSE_TP:
        calibration_mode_ = "efuse_two_point";
        break;

      case ESP_ADC_CAL_VAL_EFUSE_VREF:
        calibration_mode_ = "efuse_vref";
        break;

      default:
        calibration_mode_ = "default_vref_1100mV";
        break;
    }

    ESP_LOGI("CalADC", "ADC calibration mode: %s",
             calibration_mode_.c_str());

    if (calibration_mode_ == "default_vref_1100mV") {
      ESP_LOGW("CalADC",
               "ESP32 ADC running without factory calibration (fallback Vref)");
    }
  }

  // -------------------------------------------------------------------------
  // Must be called explicitly (custom Sensor subclasses are not auto-enabled)
  // -------------------------------------------------------------------------
  void enable() {
    float rate = (read_rate_hz_ > 0.1f) ? read_rate_hz_ : 0.1f;
    uint32_t interval_ms = static_cast<uint32_t>(1000.0f / rate);

    sensesp_app->get_event_loop()->onRepeat(
        interval_ms,
        [this]() { this->read(); });
  }

  // -------------------------------------------------------------------------
  // Publish calibration mode to Signal K (static string)
  // -------------------------------------------------------------------------
void publish_calibration_mode(const char* sk_path) {
  auto* sk = new SKOutputString(sk_path);
  auto* value = new ObservableValue<String>(calibration_mode_);
  value->connect_to(sk);
}

 private:
  int pin_;
  float read_rate_hz_;
  adc1_channel_t channel_;
  esp_adc_cal_characteristics_t adc_chars_;
  String calibration_mode_;

  // -------------------------------------------------------------------------
  // Perform calibrated ADC read and emit volts at ADC pin
  // -------------------------------------------------------------------------
  void read() {
    int raw = adc1_get_raw(channel_);
    uint32_t millivolts =
        esp_adc_cal_raw_to_voltage(raw, &adc_chars_);
    emit(millivolts / 1000.0f);
  }

  // -------------------------------------------------------------------------
  // Map GPIO → ADC1 channel (ESP32)
  // -------------------------------------------------------------------------
  adc1_channel_t adc1_channel_for_pin(int pin) {
    switch (pin) {
      case 36: return ADC1_CHANNEL_0;  // VP
      case 37: return ADC1_CHANNEL_1;
      case 38: return ADC1_CHANNEL_2;
      case 39: return ADC1_CHANNEL_3;  // VN
      case 32: return ADC1_CHANNEL_4;
      case 33: return ADC1_CHANNEL_5;
      case 34: return ADC1_CHANNEL_6;
      case 35: return ADC1_CHANNEL_7;

      default:
        ESP_LOGE("CalADC", "Unsupported ADC pin: %d", pin);
        return ADC1_CHANNEL_0;  // fail-safe
    }
  }
};
