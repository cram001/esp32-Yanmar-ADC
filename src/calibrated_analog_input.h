#pragma once

#include "sensesp/sensors/sensor.h"
#include "sensesp_app.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_log.h>

class CalibratedAnalogInput : public sensesp::FloatSensor {
 public:
  CalibratedAnalogInput(int pin, float read_interval,
                        const String& config_path = "")
    : FloatSensor(config_path),
      pin_(pin),
      read_interval_(read_interval) {

    channel_ = adc1_channel_for_pin(pin_);

    // Configure ADC1 width + attenuation
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel_, ADC_ATTEN_DB_11);

    // FireBeetle ESP32-E uses 2.5 V reference
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        2500,            // millivolts
        &adc_chars_
    );

    ESP_LOGI("CalADC", "ADC Calibration: %s",
      cal_type == ESP_ADC_CAL_VAL_EFUSE_TP    ? "Two Point" :
      cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF  ? "eFuse Vref" :
                                                "Default Vref");

    //
    // Schedule periodic ADC reads
    //
    sensesp_app->get_event_loop()->onRepeat(
        1000.0f / read_interval_,
        [this]() { this->read(); }
    );
  }

 private:
  int pin_;
  float read_interval_;
  adc1_channel_t channel_;
  esp_adc_cal_characteristics_t adc_chars_;

  void read() {
    int raw = adc1_get_raw(channel_);
    uint32_t millivolts = esp_adc_cal_raw_to_voltage(raw, &adc_chars_);
    emit(millivolts / 1000.0f);  // output volts
  }

  //
  // Map FireBeetle ADC GPIO → ADC1 channel
  //
  adc1_channel_t adc1_channel_for_pin(int pin) {
    switch(pin) {
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
        return ADC1_CHANNEL_0;
    }
  }
};
