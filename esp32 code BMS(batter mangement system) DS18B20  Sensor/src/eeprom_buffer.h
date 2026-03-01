#pragma once
// eeprom_buffer.h – Circular EEPROM buffer for offline samples
#include <Arduino.h>

/// Packed sample written to EEPROM & published via MQTT
struct __attribute__((packed)) SampleRecord {
    uint32_t uptime_ms;
    float bus_V;
    float shunt_mV;
    float current_A;
    float power_W;
    float soc_percent;
    float soh_percent;
};

void     eeprom_buffer_init();
void     buffer_push(const SampleRecord& r);
bool     buffer_is_empty();

/// Pop the oldest record into `r`. Returns false if buffer empty.
bool     buffer_pop(SampleRecord& r);
