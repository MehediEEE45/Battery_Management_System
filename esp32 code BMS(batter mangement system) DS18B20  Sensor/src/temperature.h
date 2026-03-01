#pragma once
// temperature.h – DS18B20 sensor with linear calibration
#include <Arduino.h>

/// Call once in setup() to configure sensor.
void temp_init();

/// Read calibrated temperature in °C.
float temp_readCelsius();
