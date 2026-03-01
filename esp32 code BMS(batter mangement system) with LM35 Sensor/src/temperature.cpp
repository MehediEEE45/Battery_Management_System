#include "temperature.h"
#include "config.h"
#include "esp_adc_cal.h"

static float slope  = LM35_SLOPE_DEFAULT;
static float offset = LM35_OFFSET_DEFAULT;
static esp_adc_cal_characteristics_t adc_chars;

void temp_init() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // 0-3.3 V

    esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        DEFAULT_VREF,
        &adc_chars);

    Serial.println("[Temp] LM35 ADC1 ready (GPIO " + String(LM35_PIN) + ")");
}

float temp_readCelsius() {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(LM35_PIN);
        vTaskDelay(pdMS_TO_TICKS(5));  // FreeRTOS-friendly delay
    }
    uint32_t raw  = sum / ADC_SAMPLES;
    uint32_t mv   = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
    float rawTemp = mv / 10.0f;                       // LM35 = 10 mV/°C
    float cal     = (slope * rawTemp) + offset;
    return cal;
}
