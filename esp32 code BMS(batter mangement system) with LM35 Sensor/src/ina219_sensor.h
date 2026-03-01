#pragma once
// ina219_sensor.h – INA219 current/voltage + Coulomb-counting SoC/SoH
#include <Arduino.h>
#include "eeprom_buffer.h"

/// Initialise INA219 on a secondary I2C bus. Returns true if sensor found.
bool ina_init();

/// Was the sensor detected at boot?
bool ina_isPresent();

/// Read sensor, update SoC/SoH, and fill a SampleRecord.
///   `now` = millis() timestamp used for Coulomb integration.
void ina_readSample(unsigned long now, SampleRecord& rec);

/// Getters for display/logging
float ina_getSoC();
float ina_getSoH();
