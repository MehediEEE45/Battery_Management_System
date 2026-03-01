#pragma once
// mqtt_manager.h – MQTT connection, publish helpers, offline flush
#include <PubSubClient.h>
#include "eeprom_buffer.h"

/// Call once in setup() after WiFi is ready.
void mqtt_init();

/// Ensure connection; returns true if connected.
bool mqtt_connect();

/// Must be called every loop() iteration.
void mqtt_loop();

/// Returns true when broker link is alive.
bool mqtt_isConnected();

/// Publish a full sample with calibrated temperature.
bool mqtt_publishSample(const SampleRecord& rec, float temperature);

/// Drain any offline-buffered records through MQTT.
void mqtt_flushBuffer();
