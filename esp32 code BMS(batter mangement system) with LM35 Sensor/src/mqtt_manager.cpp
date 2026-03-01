#include "mqtt_manager.h"
#include "config.h"
#include "wifi_manager.h"
#include "relay_control.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── Internals ──
static WiFiClientSecure secureClient;
static PubSubClient     client(secureClient);

static void onMessage(char* topic, byte* payload, unsigned int len) {
    // Print raw message
    Serial.printf("[MQTT] Msg [%s]: ", topic);
    for (unsigned int i = 0; i < len; i++) Serial.print((char)payload[i]);
    Serial.println();

    // ── Legacy plain-text command ────────────────────────────
    if (len == 6 && strncmp((char*)payload, "TOGGLE", 6) == 0) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        return;
    }

    // ── JSON command parsing ──────────────────────────────────
    JsonDocument doc;
    char msgBuf[257];
    size_t copyLen = (len < 256) ? len : 256;
    memcpy(msgBuf, payload, copyLen);
    msgBuf[copyLen] = '\0';

    if (deserializeJson(doc, msgBuf) != DeserializationError::Ok) return;

    const char* cmd = doc["command"] | "";

    // ── Relay commands ────────────────────────────────────────
    if (strcmp(cmd, "relay_on") == 0) {
        relay_set(RELAY_CLOSED, REASON_MANUAL);
        Serial.println("[MQTT] Command: relay CLOSED (charge ON)");
    }
    else if (strcmp(cmd, "relay_off") == 0) {
        relay_set(RELAY_OPEN, REASON_MANUAL);
        Serial.println("[MQTT] Command: relay OPEN (charge OFF)");
    }
    else if (strcmp(cmd, "relay_toggle") == 0) {
        bool isOpen = (relay_getState() == RELAY_OPEN);
        relay_set(isOpen ? RELAY_CLOSED : RELAY_OPEN, REASON_MANUAL);
        Serial.println("[MQTT] Command: relay toggled");
    }
    else if (strcmp(cmd, "relay_auto") == 0) {
        // Clear manual lock so threshold logic resumes control
        // Achieved by forcing a known non-manual reason:
        if (relay_getCutoffReason() == REASON_MANUAL) {
            relay_set(relay_getState(), REASON_NONE);  // keep state, clear reason
        }
        Serial.println("[MQTT] Command: relay returned to auto mode");
    }
    // ── AH counter reset ──────────────────────────────────────
    else if (strcmp(cmd, "reset_ah") == 0) {
        relay_resetAh();
        Serial.println("[MQTT] Command: AH counter reset");
    }
    // ── LED command ───────────────────────────────────────────
    else if (strcmp(cmd, "led_toggle") == 0) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
    // ── ESP32 restart ─────────────────────────────────────────
    else if (strcmp(cmd, "restart") == 0) {
        Serial.println("[MQTT] Command: restarting ESP32...");
        delay(500);
        ESP.restart();
    }
}

// ── Public API ──
void mqtt_init() {
    secureClient.setInsecure();              // skip CA check (dev)
    client.setServer(MQTT_BROKER, MQTT_PORT);
    client.setCallback(onMessage);
    client.setBufferSize(512);               // room for larger JSON
}

bool mqtt_isConnected() {
    return client.connected();
}

bool mqtt_connect() {
    if (client.connected()) return true;
    if (!wifi_isConnected()) return false;

    String id = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("[MQTT] Connecting... ");

    if (client.connect(id.c_str(), MQTT_USER, MQTT_PASSWORD)) {
        Serial.println("OK");
        client.subscribe(SUB_TOPIC);
        mqtt_flushBuffer();                  // drain offline cache
        return true;
    }
    Serial.printf("FAIL rc=%d\n", client.state());
    return false;
}

void mqtt_loop() {
    client.loop();
}

bool mqtt_publishSample(const SampleRecord& rec, float temperature) {
    if (!client.connected()) return false;
    char buf[420];
    snprintf(buf, sizeof(buf),
        "{\"uptime_ms\":%u,"
         "\"bus_V\":%.3f,"
         "\"shunt_mV\":%.3f,"
         "\"current_A\":%.3f,"
         "\"power_W\":%.3f,"
         "\"temperature\":%.2f,"
         "\"soc_percent\":%.2f,"
         "\"soh_percent\":%.2f,"
         "\"ah_used\":%.4f,"
         "\"ah_rated\":%.2f,"
         "\"ah_percent\":%.1f,"
         "\"relay\":%d,"
         "\"relay_reason\":\"%s\"}",
        (unsigned)rec.uptime_ms,
        rec.bus_V, rec.shunt_mV,
        rec.current_A, rec.power_W,
        temperature,
        rec.soc_percent, rec.soh_percent,
        relay_getAhUsed(),
        (float)BATTERY_RATED_AH,
        (relay_getAhUsed() / BATTERY_RATED_AH) * 100.0f,
        (int)relay_getState(),
        relay_getReasonStr());
    return client.publish(PUB_TOPIC, buf);
}

void mqtt_flushBuffer() {
    SampleRecord r;
    while (!buffer_is_empty()) {
        if (!client.connected()) return;
        if (!buffer_pop(r)) return;
        // Publish without temperature (historical record)
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"uptime_ms\":%u,"
             "\"bus_V\":%.3f,"
             "\"shunt_mV\":%.3f,"
             "\"current_A\":%.3f,"
             "\"power_W\":%.3f,"
             "\"soc_percent\":%.2f,"
             "\"soh_percent\":%.2f,"
             "\"offline\":true}",
            (unsigned)r.uptime_ms,
            r.bus_V, r.shunt_mV,
            r.current_A, r.power_W,
            r.soc_percent, r.soh_percent);
        if (!client.publish(PUB_TOPIC, buf)) return;
        vTaskDelay(pdMS_TO_TICKS(20));  // FreeRTOS-friendly delay
    }
}
