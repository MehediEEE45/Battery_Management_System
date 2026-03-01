#pragma once
// wifi_manager.h – Multi-network WiFi helper
#include <WiFi.h>

/// Try each configured SSID in order; returns true if connected.
bool wifi_connect();

/// Returns true when station is associated and has an IP.
bool wifi_isConnected();
