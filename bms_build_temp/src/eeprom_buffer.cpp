#include "eeprom_buffer.h"
#include "config.h"
#include <EEPROM.h>

// ── Layout ──
static const int METADATA_ADDR  = 0;   // head(2) + tail(2) + count(2)
static const int METADATA_SIZE  = 6;
static const int RECORD_SIZE    = sizeof(SampleRecord);
static const int RECORDS_START  = METADATA_ADDR + METADATA_SIZE;
static const int MAX_RECORDS    = (EEPROM_TOTAL_SIZE - METADATA_SIZE) / RECORD_SIZE;

static uint16_t buf_head  = 0;
static uint16_t buf_tail  = 0;
static uint16_t buf_count = 0;

// ── Helpers ──
static void write_metadata() {
    EEPROM.put(METADATA_ADDR + 0, buf_head);
    EEPROM.put(METADATA_ADDR + 2, buf_tail);
    EEPROM.put(METADATA_ADDR + 4, buf_count);
    EEPROM.commit();
}

static void read_metadata() {
    EEPROM.get(METADATA_ADDR + 0, buf_head);
    EEPROM.get(METADATA_ADDR + 2, buf_tail);
    EEPROM.get(METADATA_ADDR + 4, buf_count);
    if (buf_head  >= MAX_RECORDS) buf_head  = 0;
    if (buf_tail  >= MAX_RECORDS) buf_tail  = 0;
    if (buf_count >  MAX_RECORDS) buf_count = 0;
}

static void write_record(uint16_t idx, const SampleRecord& r) {
    int addr = RECORDS_START + (idx % MAX_RECORDS) * RECORD_SIZE;
    EEPROM.put(addr, r);
}

static void read_record(uint16_t idx, SampleRecord& r) {
    int addr = RECORDS_START + (idx % MAX_RECORDS) * RECORD_SIZE;
    EEPROM.get(addr, r);
}

// ── Public API ──
void eeprom_buffer_init() {
    EEPROM.begin(EEPROM_TOTAL_SIZE);
    read_metadata();
    Serial.printf("[EEPROM] Buffer loaded: %u records cached\n", buf_count);
}

bool buffer_is_empty() {
    return buf_count == 0;
}

void buffer_push(const SampleRecord& r) {
    if (buf_count >= MAX_RECORDS) {
        // overwrite oldest
        buf_tail = (buf_tail + 1) % MAX_RECORDS;
        buf_count = MAX_RECORDS - 1;
    }
    write_record(buf_head, r);
    buf_head = (buf_head + 1) % MAX_RECORDS;
    buf_count++;
    if (buf_count > MAX_RECORDS) buf_count = MAX_RECORDS;
    write_metadata();
}

bool buffer_pop(SampleRecord& r) {
    if (buffer_is_empty()) return false;
    read_record(buf_tail, r);
    buf_tail = (buf_tail + 1) % MAX_RECORDS;
    if (buf_count > 0) buf_count--;
    write_metadata();
    return true;
}
