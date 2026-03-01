#include "temperature.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire oneWire(DS18B20_PIN);
static DallasTemperature sensors(&oneWire);

inline float calibrate(float raw) {
  return CAL_SLOPE * raw + CAL_OFFSET;
}

void temp_init() {
    sensors.begin();
    Serial.println("[Temp] DS18B20 ready (GPIO " + String(DS18B20_PIN) + ")");
    Serial.print("  Slope : "); Serial.println(CAL_SLOPE, 4);
    Serial.print("  Offset: "); Serial.println(CAL_OFFSET, 4);

    uint8_t deviceCount = sensors.getDeviceCount();
    Serial.print("Found DS18B20 devices: ");
    Serial.println(deviceCount);
}

float temp_readCelsius() {
    sensors.requestTemperatures();
    float rawC = sensors.getTempCByIndex(0);
    
    if (rawC == DEVICE_DISCONNECTED_C) {
        Serial.println("[Temp] Error: sensor disconnected");
        return 999.0f; // Return a high temperature to trigger cutoff/alert if sensor fails
    }
    
    return calibrate(rawC);
}
