#include "ina219_sensor.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

// ── Private state ──
static TwoWire        I2C_INA(1);
static Adafruit_INA219 ina;
static bool            present = false;

static float remaining_mAh      = 0.0f;
static float consumed_mAh       = 0.0f;
static float soc_percent        = INITIAL_SOC_PERCENT;
static float soh_percent        = 100.0f;
static unsigned long lastIntMs  = 0;

// ── Public API ──
bool ina_init() {
    I2C_INA.begin(INA_SDA_PIN, INA_SCL_PIN, 100000);

    // Scan bus
    Serial.println("[INA] Scanning I2C bus 1...");
    for (uint8_t a = 1; a < 127; ++a) {
        I2C_INA.beginTransmission(a);
        if (I2C_INA.endTransmission() == 0)
            Serial.printf("[INA]   Found 0x%02X\n", a);
    }

    present = ina.begin(&I2C_INA);
    if (!present) present = ina.begin();  // fallback default bus
    Serial.printf("[INA] %s\n", present ? "INA219 OK" : "INA219 NOT FOUND");

    // Initialise Coulomb-counting
    remaining_mAh = BATTERY_CAPACITY_mAh * (INITIAL_SOC_PERCENT / 100.0f);
    soh_percent   = (MEASURED_CAPACITY_mAh > 0.0f)
                        ? (MEASURED_CAPACITY_mAh / BATTERY_CAPACITY_mAh) * 100.0f
                        : 100.0f;
    lastIntMs = millis();
    return present;
}

bool ina_isPresent() { return present; }
float ina_getSoC()   { return soc_percent; }
float ina_getSoH()   { return soh_percent; }

void ina_readSample(unsigned long now, SampleRecord& rec) {
    float shunt_mV   = ina.getShuntVoltage_mV();
    float bus_V      = ina.getBusVoltage_V();
    float current_mA = ina.getCurrent_mA();
    float power_mW   = ina.getPower_mW();

    // Coulomb counting
    unsigned long dt_ms = now - lastIntMs;
    if (dt_ms > 0) {
        float dt_h     = (float)dt_ms / 3600000.0f;
        float delta    = current_mA * dt_h;
        consumed_mAh  += delta;
        remaining_mAh -= delta;
        remaining_mAh  = constrain(remaining_mAh, 0.0f, BATTERY_CAPACITY_mAh);
        lastIntMs       = now;
    }

    soc_percent = constrain((remaining_mAh / BATTERY_CAPACITY_mAh) * 100.0f,
                            0.0f, 100.0f);
    if (MEASURED_CAPACITY_mAh > 0.0f)
        soh_percent = (MEASURED_CAPACITY_mAh / BATTERY_CAPACITY_mAh) * 100.0f;

    rec.uptime_ms   = (uint32_t)now;
    rec.bus_V       = bus_V;
    rec.shunt_mV    = shunt_mV;
    rec.current_A   = current_mA / 1000.0f;
    rec.power_W     = power_mW   / 1000.0f;
    rec.soc_percent = soc_percent;
    rec.soh_percent = soh_percent;
}
