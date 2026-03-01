#pragma once
// display_manager.h – OLED SSD1306 display pages
#include <Arduino.h>
#include "eeprom_buffer.h"

/// Initialise OLED on I2C bus 0. Returns true if display found.
bool display_init();

/// Is the OLED available?
bool display_isPresent();

/// Show a simple single-line status message.
void display_status(const char* line1, const char* line2 = nullptr);

/// Main sensor page: V, I, P, T, SoC, SoH + relay status.
void display_sensorPage(const SampleRecord& rec, float tempC, int relayState = 0);

/// Temperature-only page (when INA219 is absent) + relay status.
void display_tempOnlyPage(float tempC, int relayState = 0);
