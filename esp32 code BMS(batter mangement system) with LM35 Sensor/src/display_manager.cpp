#include "display_manager.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Private state ──
static TwoWire          I2C_OLED(0);
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &I2C_OLED, -1);
static bool             present = false;

// ── Public API ──
bool display_init() {
    I2C_OLED.begin(OLED_SDA_PIN, OLED_SCL_PIN, 400000);

    // Quick scan
    Serial.println("[OLED] Scanning I2C bus 0...");
    for (uint8_t a = 1; a < 127; ++a) {
        I2C_OLED.beginTransmission(a);
        if (I2C_OLED.endTransmission() == 0)
            Serial.printf("[OLED]   Found 0x%02X\n", a);
    }

    present = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    if (present) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 0);
        oled.println("OLED OK");
        oled.display();
    }
    Serial.printf("[OLED] %s\n", present ? "OK" : "NOT FOUND");
    return present;
}

bool display_isPresent() { return present; }

void display_status(const char* line1, const char* line2) {
    if (!present) return;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println(line1);
    if (line2) oled.println(line2);
    oled.display();
}


void display_sensorPage(const SampleRecord& rec, float tempC, int relayState) {
    if (!present) return;
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    // --- Row 1: Voltage & Current (Big Font) ---
    oled.setTextSize(2); 
    
    // Voltage (Left)
    oled.setCursor(0, 0);
    oled.print(rec.bus_V, 1); oled.print("V");

    // Current (Right)
    oled.setCursor(70, 0);
    oled.print(rec.current_A, 1); oled.print("A");

    // --- Row 2: Power & Temp (Big Font) ---
    // Power (Left)
    oled.setCursor(0, 24);
    oled.print(rec.power_W, 0); oled.print("W"); // 0 decimals for Power to save space

    // Temperature (Right)
    oled.setCursor(70, 24);
    oled.print((int)tempC); oled.print("C");

    // --- Decorative Divider ---
    oled.drawFastHLine(0, 44, 128, SSD1306_WHITE);

    // --- Bottom Row: Status (Small Font) ---
    oled.setTextSize(1);
    oled.setCursor(0, 52);
    if (relayState == 0) {
        oled.print("STATUS: CHARGING...");
    } else {
        oled.print("STATUS: IDLE");
    }

    oled.display();
}

// // void display_sensorPage(const SampleRecord& rec, float tempC, int relayState) {
//     if (!present) return;
//     oled.clearDisplay();

//     // ── Row 0: V and relay badge ─────────────────────────────
//     oled.setTextSize(1);
//     oled.setCursor(0, 0);
//     oled.print("V:"); oled.print(String(rec.bus_V, 3)); oled.print("V");
//     // Relay badge: top-right corner
//     oled.setCursor(90, 0);
//     if (relayState == 0) {    // RELAY_CLOSED = charging
//         oled.print("[CHG ON]");
//     } else {                  // RELAY_OPEN   = stopped
//         oled.print("[CHG OFF]");
//     }

//     // ── Row 1: I ──────────────────────────────────────────────
//     oled.setCursor(0, 10);
//     oled.print("I:"); oled.print(String(rec.current_A, 3)); oled.print("A");

//     // ── Row 2: P ──────────────────────────────────────────────
//     oled.setCursor(0, 20);
//     oled.print("P:"); oled.print(String(rec.power_W, 2)); oled.print("W");

//     // ── Row 3: T ──────────────────────────────────────────────
//     oled.setCursor(0, 30);
//     oled.print("T:"); oled.print(String(tempC, 1)); oled.print("C");

//     // ── Row 4: SoC / SoH ──────────────────────────────────────
//     oled.setCursor(0, 40);
//     oled.print("SoC:"); oled.print(String(rec.soc_percent, 1)); oled.print("%");
//     oled.setCursor(70, 40);
//     oled.print("SoH:"); oled.print(String(rec.soh_percent, 1)); oled.print("%");

//     // ── Row 5: SoC bar ────────────────────────────────────────
//     oled.setCursor(0, 52);
//     oled.print("[");
//     int barPixels = (int)(rec.soc_percent / 100.0f * 20);  // 20 chars wide
//     for (int i = 0; i < 20; i++) {
//         oled.print(i < barPixels ? "|" : " ");
//     }
//     oled.print("]");

//     oled.display();
// }

void display_tempOnlyPage(float tempC, int relayState) {
    if (!present) return;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    // Relay badge
    oled.print(relayState == 0 ? "Relay: CHG ON" : "Relay: CHG OFF");
    oled.setTextSize(2);
    oled.setCursor(0, 14);
    oled.print("T:");
    oled.setCursor(0, 30);
    oled.print(String(tempC, 1));
    oled.print("C");
    oled.setTextSize(1);
    oled.setCursor(0, 56);
    oled.print("INA219 not found");
    oled.display();
}
