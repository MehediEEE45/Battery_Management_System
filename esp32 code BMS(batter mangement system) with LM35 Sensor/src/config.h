#pragma once
// ===============================================================
//  config.h  –  All project-wide constants, pins & credentials
// ===============================================================

#include <Arduino.h>

// ───── LED ─────
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ───── LM35 Temperature Sensor ─────
// ** GPIO 32 is on ADC1 – safe to use while WiFi is active **
#define LM35_PIN        32
#define DEFAULT_VREF    1100   // mV (eFuse fallback)
#define ADC_SAMPLES     10     // averaging window

// LM35 2-point calibration: temp = SLOPE * raw + OFFSET
#define LM35_SLOPE_DEFAULT   1.03f
#define LM35_OFFSET_DEFAULT -5.0f   // °C

// ───── WiFi (tried in order) ─────
struct WifiCred { const char* ssid; const char* password; };
static const WifiCred WIFI_CREDENTIALS[] = {
    { "Teachers_WiFi_SUST", "SUST11s34"   },
    { "SUST WiFi",          "SUST10s10"   },
    { "MiM",                "Ha20202021"  },
};
static const size_t WIFI_CRED_COUNT =
    sizeof(WIFI_CREDENTIALS) / sizeof(WIFI_CREDENTIALS[0]);
static const unsigned long WIFI_TIMEOUT_MS = 15000;  // per SSID

// ───── MQTT (HiveMQ Cloud, TLS) ─────
#define MQTT_BROKER   "0d34f5789e1e4a669367abfe5bd45b15.s1.eu.hivemq.cloud"
#define MQTT_PORT     8883
#define MQTT_USER     "battery"
#define MQTT_PASSWORD "Batterybms80"
#define PUB_TOPIC     "battery/data"
#define SUB_TOPIC     "battery/recieve"

// ───── I2C Pins ─────
#define OLED_SDA_PIN  21
#define OLED_SCL_PIN  22
#define INA_SDA_PIN    5
#define INA_SCL_PIN    4

// ───── OLED ─────
#define OLED_ADDRESS  0x3C
#define OLED_WIDTH    128
#define OLED_HEIGHT   64

// ───── Misc Pins ─────
#define BUTTON_PIN    25

// ───── Charge Relay ─────
#define RELAY_PIN           14
#define RELAY_ACTIVE_LEVEL  LOW    // LOW = energise coil = circuit closed

// Auto cut-off thresholds (open relay = stop charging)
#define RELAY_CUTOFF_SOC_PERCENT  95.0f   // cut when SoC  ≥ 95 %
#define RELAY_CUTOFF_VOLTAGE_V     4.15f  // cut when V    ≥ 4.15 V
#define RELAY_CUTOFF_TEMP_C        45.0f  // cut when Temp ≥ 45 °C

// Auto resume thresholds (close relay = resume charging)
#define RELAY_RESUME_SOC_PERCENT   85.0f  // resume when SoC  ≤ 85 %
#define RELAY_RESUME_VOLTAGE_V      4.05f // resume when V    ≤ 4.05 V
#define RELAY_RESUME_TEMP_C         40.0f // resume when Temp ≤ 40 °C

// ───── Battery / SoC ─────
#define BATTERY_CAPACITY_mAh    4200.0f
#define INITIAL_SOC_PERCENT     100.0f
// Measured capacity from 3000 mWh @ 3.7 V → ~810.81 mAh
#define MEASURED_CAPACITY_mAh   810.81f

// ───── AH counting (discharge protection) ─────
// Rated capacity in Amp-Hours (= BATTERY_CAPACITY_mAh / 1000)
#define BATTERY_RATED_AH           4.2f    // Ah
// Relay opens when consumed AH reaches this % of rated capacity
#define RELAY_CUTOFF_AH_PERCENT   50.0f    // disconnect at 50 % DoD

// ───── Timing ─────
#define PUBLISH_INTERVAL_MS     5000
#define DISPLAY_INTERVAL_MS     1000
#define WATCHDOG_INTERVAL_MS    10000
#define WIFI_CHECK_INTERVAL_MS  5000

// ───── FreeRTOS Task Configuration ─────
// Core assignments (ESP32 has Core 0 and Core 1)
// Core 0: WiFi/BT stack runs here – network tasks go here
// Core 1: Arduino loop() core – sensor + display tasks go here
#define TASK_SENSOR_CORE        1
#define TASK_NETWORK_CORE       0
#define TASK_DISPLAY_CORE       1
#define TASK_WATCHDOG_CORE      0

// Task priorities (higher = more important, max ~24)
#define TASK_SENSOR_PRIORITY    3
#define TASK_NETWORK_PRIORITY   2
#define TASK_DISPLAY_PRIORITY   1
#define TASK_WATCHDOG_PRIORITY  1

// Stack sizes (bytes)
#define TASK_SENSOR_STACK       4096
#define TASK_NETWORK_STACK      8192   // needs room for TLS
#define TASK_DISPLAY_STACK      4096
#define TASK_WATCHDOG_STACK     2048
#define TASK_RELAY_STACK        2048   // relay monitoring task

// Hardware watchdog timeout (seconds)
#define WDT_TIMEOUT_SEC         30

// ───── EEPROM Buffering ─────
#define EEPROM_TOTAL_SIZE       4096
