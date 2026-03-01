// ═══════════════════════════════════════════════════════════════════
//  main.cpp – FreeRTOS multi-task architecture for Battery Monitor
// ═══════════════════════════════════════════════════════════════════
//
//  Task layout (ESP32 dual-core):
//  ┌──────────────┬───────┬──────┬────────────────────────────────┐
//  │ Task         │ Core  │ Prio │ Role                           │
//  ├──────────────┼───────┼──────┼────────────────────────────────┤
//  │ SensorTask   │  1    │  3   │ Read INA219 + LM35 every 5 s   │
//  │ NetworkTask  │  0    │  2   │ WiFi + MQTT + publish          │
//  │ DisplayTask  │  1    │  1   │ Update OLED every 1 s          │
//  │ WatchdogTask │  0    │  1   │ Feed HW watchdog, health check │
//  └──────────────┴───────┴──────┴────────────────────────────────┘
//
//  Shared data protected by `xSensorMutex` (FreeRTOS mutex).
// ═══════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "temperature.h"
#include "ina219_sensor.h"
#include "display_manager.h"
#include "eeprom_buffer.h"
#include "relay_control.h"

// ─────────────── Shared state (mutex-protected) ──────────────────
static SemaphoreHandle_t xSensorMutex = NULL;

struct SharedSensorData {
    SampleRecord rec;
    float        temperature;
    bool         dataReady;      // true after first sensor read
    bool         wifiConnected;
    bool         mqttConnected;
    uint32_t     sensorReadCount;
    uint32_t     publishCount;
    uint32_t     failCount;
    RelayState   relayState;     // current relay position
    const char*  relayReason;    // last cut-off reason string
    float        ahUsed;         // accumulated Amp-Hours consumed
};
static SharedSensorData shared = {};

// ─────────────── Task handles ────────────────────────────────────
static TaskHandle_t hSensorTask   = NULL;
static TaskHandle_t hNetworkTask  = NULL;
static TaskHandle_t hDisplayTask  = NULL;
static TaskHandle_t hWatchdogTask = NULL;

// ═════════════════════════════════════════════════════════════════
//  TASK 1 – SENSOR (Core 1, Priority 3)
//  Reads INA219 + LM35 every PUBLISH_INTERVAL_MS.
//  Never blocked by WiFi/MQTT — always keeps measuring.
// ═════════════════════════════════════════════════════════════════
void sensorTask(void* pvParam) {
    Serial.println("[SensorTask] Started on core " + String(xPortGetCoreID()));

    for (;;) {
        unsigned long now = millis();
        float tempC = temp_readCelsius();

        SampleRecord rec = {};
        if (ina_isPresent()) {
            ina_readSample(now, rec);
        } else {
            rec.uptime_ms = (uint32_t)now;
        }

        // ── Relay threshold check (runs every sensor cycle) ────────
        relay_checkThresholds(rec, tempC);

        // Write to shared state under mutex
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            shared.rec         = rec;
            shared.temperature = tempC;
            shared.dataReady   = true;
            shared.sensorReadCount++;
            shared.relayState  = relay_getState();
            shared.relayReason = relay_getReasonStr();
            shared.ahUsed      = relay_getAhUsed();
            xSemaphoreGive(xSensorMutex);
        }

        Serial.printf("[Sensor] V=%.3f I=%.3f P=%.3f T=%.2f SoC=%.1f%%  AH=%.4f/%.2f  Relay:%s  (#%u)\n",
                       rec.bus_V, rec.current_A, rec.power_W, tempC,
                       rec.soc_percent,
                       relay_getAhUsed(),
                       (RELAY_CUTOFF_AH_PERCENT / 100.0f) * BATTERY_RATED_AH,
                       relay_getState() == RELAY_CLOSED ? "ON" : "OFF",
                       shared.sensorReadCount);

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

// ═════════════════════════════════════════════════════════════════
//  TASK 2 – NETWORK (Core 0, Priority 2)
//  Manages WiFi reconnection, MQTT keepalive, publishes sensor
//  data, flushes offline buffer. Runs every 5 s.
// ═════════════════════════════════════════════════════════════════
void networkTask(void* pvParam) {
    Serial.println("[NetworkTask] Started on core " + String(xPortGetCoreID()));

    // Wait for first sensor reading
    while (!shared.dataReady) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    for (;;) {
        // 1. WiFi
        if (!wifi_isConnected()) {
            Serial.println("[Network] WiFi reconnecting...");
            wifi_connect();
        }

        // 2. MQTT
        if (wifi_isConnected() && !mqtt_isConnected()) {
            Serial.println("[Network] MQTT reconnecting...");
            mqtt_connect();
        }
        mqtt_loop();   // PubSubClient keepalive + incoming

        // Update connection flags under mutex
        bool wifiOk = wifi_isConnected();
        bool mqttOk = mqtt_isConnected();
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            shared.wifiConnected = wifiOk;
            shared.mqttConnected = mqttOk;
            xSemaphoreGive(xSensorMutex);
        }

        // 3. Read latest sensor data
        SampleRecord rec;
        float        tempC;
        bool         ready = false;
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            rec   = shared.rec;
            tempC = shared.temperature;
            ready = shared.dataReady;
            xSemaphoreGive(xSensorMutex);
        }

        // 4. Publish or buffer
        if (ready) {
            bool online = wifiOk && mqttOk;
            if (online && mqtt_publishSample(rec, tempC)) {
                if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    shared.publishCount++;
                    xSemaphoreGive(xSensorMutex);
                }
                Serial.printf("[Network] Published #%u V=%.3f T=%.2f\n",
                              shared.publishCount, rec.bus_V, tempC);
                mqtt_flushBuffer();   // drain offline cache
            } else {
                if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    shared.failCount++;
                    xSemaphoreGive(xSensorMutex);
                }
                Serial.println("[Network] Offline – buffering sample");
                buffer_push(rec);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

// ═════════════════════════════════════════════════════════════════
//  TASK 3 – DISPLAY (Core 1, Priority 1)
//  Updates OLED at DISPLAY_INTERVAL_MS with latest sensor data
//  and connection status. Low priority so sensor task runs first.
// ═════════════════════════════════════════════════════════════════
void displayTask(void* pvParam) {
    Serial.println("[DisplayTask] Started on core " + String(xPortGetCoreID()));

    for (;;) {
        SampleRecord rec;
        float        tempC      = 0;
        bool         ready      = false;
        bool         wifiOk     = false;
        bool         mqttOk     = false;
        uint32_t     pubCount   = 0;
        uint32_t     failCount  = 0;
        RelayState   relayState = RELAY_OPEN;
        const char*  relayReason = "";

        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            rec         = shared.rec;
            tempC       = shared.temperature;
            ready       = shared.dataReady;
            wifiOk      = shared.wifiConnected;
            mqttOk      = shared.mqttConnected;
            pubCount    = shared.publishCount;
            failCount   = shared.failCount;
            relayState  = shared.relayState;
            relayReason = shared.relayReason ? shared.relayReason : "";
            xSemaphoreGive(xSensorMutex);
        }

        if (ready) {
            if (ina_isPresent()) {
                display_sensorPage(rec, tempC, relayState);
            } else {
                display_tempOnlyPage(tempC, relayState);
            }
        } else {
            // Waiting for sensor data
            char statusLine[32];
            snprintf(statusLine, sizeof(statusLine), "WiFi:%s MQTT:%s",
                     wifiOk ? "OK" : "--", mqttOk ? "OK" : "--");
            display_status("Waiting sensors...", statusLine);
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_INTERVAL_MS));
    }
}

// ═════════════════════════════════════════════════════════════════
//  TASK 4 – WATCHDOG (Core 0, Priority 1)
//  Feeds hardware watchdog timer, prints system health stats.
//  If any task hangs, the WDT resets the ESP32 automatically.
// ═════════════════════════════════════════════════════════════════
void watchdogTask(void* pvParam) {
    Serial.println("[WatchdogTask] Started on core " + String(xPortGetCoreID()));

    // Register this task with hardware WDT
    esp_task_wdt_add(NULL);

    for (;;) {
        // Feed the watchdog
        esp_task_wdt_reset();

        // Print health stats
        uint32_t freeHeap  = ESP.getFreeHeap();
        uint32_t minHeap   = ESP.getMinFreeHeap();
        UBaseType_t sensorHW  = (hSensorTask)  ? uxTaskGetStackHighWaterMark(hSensorTask)  : 0;
        UBaseType_t networkHW = (hNetworkTask)  ? uxTaskGetStackHighWaterMark(hNetworkTask) : 0;
        UBaseType_t displayHW = (hDisplayTask)  ? uxTaskGetStackHighWaterMark(hDisplayTask) : 0;

        Serial.printf("[WDT] Heap: %u free / %u min | Stack HW: sensor=%u net=%u disp=%u\n",
                       freeHeap, minHeap, sensorHW, networkHW, displayHW);

        // Check critical conditions
        if (freeHeap < 10000) {
            Serial.println("[WDT] WARNING: Low heap memory!");
        }

        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_INTERVAL_MS));
    }
}

// ═════════════════════════════════════════════════════════════════
//  SETUP – Initialise hardware, create mutex, launch tasks
// ═════════════════════════════════════════════════════════════════
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    Serial.begin(115200);
    delay(1000);

    Serial.println("========================================");
    Serial.println("  Battery Monitor – FreeRTOS Edition");
    Serial.println("========================================");

    // ── 1. Create mutex ──
    xSensorMutex = xSemaphoreCreateMutex();
    if (!xSensorMutex) {
        Serial.println("[FATAL] Could not create mutex!");
        while (1) delay(1000);
    }
    Serial.println("[Init] Mutex created");

    // ── 2. Offline buffer ──
    eeprom_buffer_init();

    // ── 3. Sensors ──
    temp_init();
    display_init();
    ina_init();
    relay_init();   // initialise relay (starts OPEN = safe)

    display_status(ina_isPresent() ? "INA219: OK" : "INA219: NOT FOUND");
    delay(800);

    // ── 4. Connectivity ──
    display_status("WiFi: connecting...");
    wifi_connect();
    if (wifi_isConnected()) {
        display_status("WiFi: connected", WiFi.localIP().toString().c_str());
        shared.wifiConnected = true;
    } else {
        display_status("WiFi: FAILED");
    }
    delay(500);

    display_status("MQTT: connecting...");
    mqtt_init();
    mqtt_connect();
    shared.mqttConnected = mqtt_isConnected();
    display_status(mqtt_isConnected() ? "MQTT: connected" : "MQTT: FAILED");
    delay(500);

    // ── 5. Hardware watchdog ──
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);  // true = panic (reset) on timeout
    Serial.printf("[Init] Watchdog: %d second timeout\n", WDT_TIMEOUT_SEC);

    // ── 6. Launch FreeRTOS tasks ──
    display_status("Starting tasks...");

    xTaskCreatePinnedToCore(
        sensorTask,            // function
        "SensorTask",          // name
        TASK_SENSOR_STACK,     // stack size (bytes)
        NULL,                  // parameter
        TASK_SENSOR_PRIORITY,  // priority
        &hSensorTask,          // handle
        TASK_SENSOR_CORE       // core
    );

    xTaskCreatePinnedToCore(
        networkTask,
        "NetworkTask",
        TASK_NETWORK_STACK,
        NULL,
        TASK_NETWORK_PRIORITY,
        &hNetworkTask,
        TASK_NETWORK_CORE
    );

    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        TASK_DISPLAY_STACK,
        NULL,
        TASK_DISPLAY_PRIORITY,
        &hDisplayTask,
        TASK_DISPLAY_CORE
    );

    xTaskCreatePinnedToCore(
        watchdogTask,
        "WatchdogTask",
        TASK_WATCHDOG_STACK,
        NULL,
        TASK_WATCHDOG_PRIORITY,
        &hWatchdogTask,
        TASK_WATCHDOG_CORE
    );

    Serial.println("[Init] All 4 tasks launched!");
    Serial.printf("[Init] Free heap: %u bytes\n", ESP.getFreeHeap());
    display_status("FreeRTOS running", "4 tasks active");
}

// ═════════════════════════════════════════════════════════════════
//  LOOP – Empty! All work done by FreeRTOS tasks above.
//  Arduino loop() runs as its own lowest-priority task on Core 1.
// ═════════════════════════════════════════════════════════════════
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));  // yield to other tasks
}

