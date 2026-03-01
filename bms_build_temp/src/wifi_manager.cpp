#include "wifi_manager.h"
#include "config.h"

bool wifi_isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool wifi_connect() {
    if (wifi_isConnected()) return true;

    Serial.println("[WiFi] Scanning configured networks...");
    WiFi.mode(WIFI_STA);

    for (size_t i = 0; i < WIFI_CRED_COUNT; ++i) {
        const char* ssid = WIFI_CREDENTIALS[i].ssid;
        const char* pass = WIFI_CREDENTIALS[i].password;
        Serial.printf("[WiFi] Trying: %s\n", ssid);

        WiFi.disconnect(true);
        delay(100);
        WiFi.begin(ssid, pass);

        unsigned long t0 = millis();
        while (!wifi_isConnected() && (millis() - t0) < WIFI_TIMEOUT_MS) {
            delay(500);
            Serial.print('.');
        }

        if (wifi_isConnected()) {
            Serial.printf("\n[WiFi] Connected to %s  IP: %s\n",
                          ssid, WiFi.localIP().toString().c_str());
            return true;
        }
        Serial.printf("\n[WiFi] Failed: %s\n", ssid);
    }

    Serial.println("[WiFi] Could not connect to any network.");
    return false;
}
