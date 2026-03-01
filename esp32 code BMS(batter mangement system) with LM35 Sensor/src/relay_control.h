#pragma once
// ================================================================
//  relay_control.h  –  Charge relay driver for Battery Monitor
// ================================================================
//
//  Circuit:
//    ESP32 GPIO26 ──► Relay module IN
//    Relay NO (Normally Open) ──► Charge + line
//    Common ──► Battery + terminal
//
//  Logic (most relay modules are LOW-trigger):
//    GPIO LOW  → Relay ENERGISED  → Charge circuit CLOSED  (charging allowed)
//    GPIO HIGH → Relay RELEASED    → Charge circuit OPEN   (charging stopped)
//
//  Auto cut-off triggers:
//    • SoC  ≥ RELAY_CUTOFF_SOC_PERCENT    (fully charged)
//    • V    ≥ RELAY_CUTOFF_VOLTAGE_V      (over-voltage)
//    • Temp ≥ RELAY_CUTOFF_TEMP_C         (over-temperature)
//
//  Auto resume triggers:
//    • SoC  ≤ RELAY_RESUME_SOC_PERCENT    (needs charge)
//    • V    ≤ RELAY_RESUME_VOLTAGE_V      (safe voltage)
//    • Temp ≤ RELAY_RESUME_TEMP_C         (cooled down)
// ================================================================

#include <Arduino.h>
#include "eeprom_buffer.h"   // for SampleRecord

enum RelayState {
    RELAY_CLOSED = 0,   // circuit closed  → charging ALLOWED
    RELAY_OPEN   = 1    // circuit open    → charging STOPPED
};

enum RelayCutoffReason {
    REASON_NONE        = 0,
    REASON_SOC_FULL    = 1,
    REASON_OVERVOLTAGE = 2,
    REASON_OVERTEMP    = 3,
    REASON_MANUAL      = 4,
    REASON_AH_LIMIT    = 5   // consumed AH reached 50 % of rated capacity
};

/// Initialise relay GPIO. Sets safe default (open = no charge).
void relay_init();

/// Force relay to a specific state. Use for manual/remote control.
void relay_set(RelayState state, RelayCutoffReason reason = REASON_MANUAL);

/// Get current relay state.
RelayState relay_getState();

/// Get the reason the relay was last opened.
RelayCutoffReason relay_getCutoffReason();

/// Get human-readable reason string.
const char* relay_getReasonStr();

/// Evaluate thresholds and auto-open/close relay.
/// Call this inside SensorTask after every reading.
void relay_checkThresholds(const SampleRecord& rec, float tempC);

/// Get accumulated Amp-Hours consumed since last reset.
float relay_getAhUsed();

/// Reset AH counter to zero (e.g. after battery change or full charge).
void relay_resetAh();
