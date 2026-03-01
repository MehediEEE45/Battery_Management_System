#pragma once
// temperature.h – LM35 sensor with ESP32 ADC calibration
#include <Arduino.h>

/// Call once in setup() to configure ADC + calibration.
void temp_init();

/// Read averaged, calibrated temperature in °C.
float temp_readCelsius();
