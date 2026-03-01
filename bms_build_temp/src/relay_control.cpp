// ================================================================
//  relay_control.cpp  –  Charge relay driver
// ================================================================
#include "relay_control.h"
#include "config.h"
#include <Arduino.h>

// ── Private state ──────────────────────────────────────────────
static RelayState        currentState  = RELAY_OPEN;   // safe default
static RelayCutoffReason cutoffReason  = REASON_NONE;

// ── AH accumulator ─────────────────────────────────────────────
static float    accum_Ah       = 0.0f;   // total Ah consumed
static uint32_t lastAhTime_ms  = 0;      // timestamp of last integration step

// ── Helpers ────────────────────────────────────────────────────
static void applyState(RelayState s) {
    // Most relay modules: LOW = energise (circuit closed)
    //                     HIGH = release  (circuit open)
    digitalWrite(RELAY_PIN, (s == RELAY_CLOSED) ? RELAY_ACTIVE_LEVEL
                                                 : !RELAY_ACTIVE_LEVEL);
    currentState = s;
}

// ── Public API ─────────────────────────────────────────────────
void relay_init() {
    pinMode(RELAY_PIN, OUTPUT);
    applyState(RELAY_OPEN);   // always start with charge stopped (safe)
    Serial.printf("[Relay] Initialized on GPIO%d  →  OPEN (charge off)\n", RELAY_PIN);
}

void relay_set(RelayState state, RelayCutoffReason reason) {
    cutoffReason = reason;
    if (state == currentState) return;   // no change
    applyState(state);
    Serial.printf("[Relay] %s  reason=%s\n",
                  state == RELAY_CLOSED ? "CLOSED (charge ON)" : "OPEN (charge OFF)",
                  relay_getReasonStr());
}

RelayState       relay_getState()        { return currentState; }
RelayCutoffReason relay_getCutoffReason() { return cutoffReason; }

const char* relay_getReasonStr() {
    switch (cutoffReason) {
        case REASON_SOC_FULL:    return "SoC full";
        case REASON_OVERVOLTAGE: return "Over-voltage";
        case REASON_OVERTEMP:    return "Over-temp";
        case REASON_MANUAL:      return "Manual";
        case REASON_AH_LIMIT:    return "AH limit";
        default:                 return "None";
    }
}

float relay_getAhUsed()  { return accum_Ah; }
void  relay_resetAh()    { accum_Ah = 0.0f; lastAhTime_ms = 0; Serial.println("[Relay] AH counter reset"); }

// ── Threshold evaluation  ──────────────────────────────────────
void relay_checkThresholds(const SampleRecord& rec, float tempC) {

    // ── AH integration (Coulomb counting) ──────────────────────
    // Accumulate |I| × dt regardless of relay / manual state
    uint32_t now_ms = millis();
    if (lastAhTime_ms > 0 && rec.current_A != 0.0f) {
        float dt_h  = (now_ms - lastAhTime_ms) / 3600000.0f;  // ms → hours
        accum_Ah   += fabsf(rec.current_A) * dt_h;
    }
    lastAhTime_ms = now_ms;

    // ── Cut-off conditions (open relay = stop charging) ────────
    if (rec.soc_percent >= RELAY_CUTOFF_SOC_PERCENT && rec.soc_percent > 0) {
        relay_set(RELAY_OPEN, REASON_SOC_FULL);
        return;
    }
    if (rec.bus_V >= RELAY_CUTOFF_VOLTAGE_V && rec.bus_V > 0) {
        relay_set(RELAY_OPEN, REASON_OVERVOLTAGE);
        return;
    }
    if (tempC >= RELAY_CUTOFF_TEMP_C && tempC > 0) {
        relay_set(RELAY_OPEN, REASON_OVERTEMP);
        return;
    }

    // ── AH limit cut-off ───────────────────────────────────────
    // Open relay when consumed AH ≥ 50 % of rated capacity
    const float cutoffAh = (RELAY_CUTOFF_AH_PERCENT / 100.0f) * BATTERY_RATED_AH;
    if (accum_Ah >= cutoffAh) {
        relay_set(RELAY_OPEN, REASON_AH_LIMIT);
        Serial.printf("[Relay] AH limit reached: %.3f Ah / %.3f Ah (%.0f%%)\n",
                      accum_Ah, cutoffAh, RELAY_CUTOFF_AH_PERCENT);
        return;
    }

    // ── Resume conditions (close relay = allow charging) ───────
    // Only resume if ALL conditions are below their resume thresholds
    // AND the relay was opened automatically (not manually forced)
    if (cutoffReason != REASON_MANUAL) {
        bool socOk  = (rec.soc_percent <= RELAY_RESUME_SOC_PERCENT) || (rec.soc_percent == 0);
        bool voltOk = (rec.bus_V <= RELAY_RESUME_VOLTAGE_V)         || (rec.bus_V == 0);
        bool tempOk = (tempC <= RELAY_RESUME_TEMP_C);

        if (socOk && voltOk && tempOk) {
            relay_set(RELAY_CLOSED, REASON_NONE);
        }
    }
}
