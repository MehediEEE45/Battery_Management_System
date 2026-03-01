# ESP32 Battery Management System (BMS)

A production-ready, modular firmware for an ESP32-based Battery Management System.  
It measures voltage, current, power, temperature, State-of-Charge (SoC), and State-of-Health (SoH) in real time, publishes data over MQTT to the cloud, displays readings on an OLED screen, and controls a charge relay with automatic safety cut-off logic — including **Amp-Hour (AH) tracking**.

---

## About This Project

Battery management is one of the most critical aspects of any energy storage system. Overcharging, over-discharging, and thermal runaway are the leading causes of battery degradation and, in serious cases, fire hazards. This project was built to address those risks with a low-cost, open-source solution based on the ESP32 microcontroller.

The system continuously monitors a lithium-ion battery pack using an **INA219** power sensor for precise voltage, current, and power readings, and an **LM35** analog temperature sensor for thermal protection. All measurements are processed on-device and used to calculate the battery's **State of Charge (SoC)** — how much energy is left — and **State of Health (SoH)** — how much capacity the battery has retained over its lifetime compared to its original rating.

A key feature of this BMS is its **Amp-Hour (AH) tracking**. Rather than relying solely on voltage thresholds, the firmware integrates current over time (`AH = ∑ |I| × Δt`) to measure actual energy drawn from the battery. When the accumulated AH reaches **50% of the rated capacity (2.1 Ah out of 4.2 Ah)**, a relay on **GPIO26** automatically disconnects the charge circuit, protecting the battery from deep discharge. This threshold, along with overvoltage, over-temperature, and SoC limits, forms a multi-layer safety system.

All data is published every 5 seconds as a structured **JSON payload over MQTT** (TLS-encrypted) to **HiveMQ Cloud**, making it available to any connected dashboard, mobile app, or backend server in real time. The firmware also supports **remote relay control** via MQTT — an operator can force the relay open or closed, reset the AH counter after a battery swap, or reboot the device, all without physical access.

To ensure reliability, the firmware uses **FreeRTOS** with four dedicated tasks pinned across the ESP32's two cores: one for sensor reading, one for network communication, one for the OLED display, and one for the hardware watchdog. If any task stalls or the system enters an inconsistent state, the 30-second watchdog timer automatically resets the device. An **EEPROM offline buffer** ensures no data is lost during temporary WiFi or MQTT outages — samples are stored locally and flushed to the cloud when the connection is restored.

This project is designed for use in solar energy systems, electric vehicle auxiliary packs, UPS units, and any application where battery health monitoring and autonomous charge control are required.

---

## Features

| Feature | Detail |
|---|---|
| **Dual-sensor measurement** | INA219 (V / I / P via I2C) + LM35 (temperature via ADC) |
| **State of Charge (SoC)** | Coulomb counting with EEPROM-backed initial value |
| **State of Health (SoH)** | Capacity fade tracking over charge cycles |
| **AH tracking** | Accumulates `AH = sum(|I| x dt)` — relay opens at 50 % of rated 4.2 Ah |
| **Charge relay control** | GPIO26, LOW-trigger — auto cut-off on SoC / voltage / temperature / AH limit |
| **OLED display** | 128x64 SSD1306, shows V / I / P / T / SoC / SoH + relay badge `[CHG ON/OFF]` |
| **MQTT over TLS** | Publishes JSON to HiveMQ Cloud every 5 s (port 8883) |
| **Remote commands** | JSON commands via MQTT: relay control, AH reset, LED toggle, restart |
| **Offline buffering** | EEPROM circular buffer stores samples when WiFi / MQTT is unavailable |
| **FreeRTOS** | 4 independent tasks across both ESP32 cores for reliable operation |
| **Hardware Watchdog** | 30-second WDT auto-resets if any task hangs |

---

## Hardware

| Component | Connection |
|---|---|
| ESP32-D0WD-V3 (DevKit) | — |
| INA219 current sensor | I2C on GPIO4 (SCL) / GPIO5 (SDA) |
| LM35 temperature sensor | Analog on GPIO32 (ADC1 — WiFi-safe) |
| SSD1306 OLED 128x64 | I2C on GPIO22 (SCL) / GPIO21 (SDA) |
| Relay module (LOW-trigger) | GPIO26 -> Relay IN, cuts charge + line |
| Push button | GPIO25, INPUT_PULLUP |
| Built-in LED | GPIO2 |

**Relay wiring:**
```
ESP32 GPIO26 --> Relay IN
Relay NO (Normally Open) --> Charge + line
Relay COM --> Battery + terminal
```
- GPIO **LOW**  → relay energised → charge circuit **CLOSED** (charging ON)
- GPIO **HIGH** → relay released  → charge circuit **OPEN**   (charging OFF)

---

## Software Architecture

### FreeRTOS Task Layout

```
+----------------+-------+------+-------------------------------------+
| Task           | Core  | Prio | Role                                |
+----------------+-------+------+-------------------------------------+
| SensorTask     |  1    |  3   | INA219 + LM35 + AH + relay check   |
| NetworkTask    |  0    |  2   | WiFi reconnect + MQTT + publish     |
| DisplayTask    |  1    |  1   | OLED update every 1 s               |
| WatchdogTask   |  0    |  1   | Feed HW WDT, heap/stack health      |
+----------------+-------+------+-------------------------------------+
```

All sensor data is protected by a FreeRTOS mutex (`xSensorMutex`).

### Source Files

```
src/
├── main.cpp              — FreeRTOS tasks, setup, shared state
├── config.h              — All pins, credentials, thresholds
├── ina219_sensor.cpp/h   — INA219 I2C driver + SoC/SoH Coulomb counting
├── temperature.cpp/h     — LM35 ADC averaging (10-sample window)
├── relay_control.cpp/h   — Relay driver + AH accumulator + threshold logic
├── display_manager.cpp/h — OLED pages (sensor data, temp-only, status)
├── mqtt_manager.cpp/h    — MQTT connect + publish JSON + command handler
├── wifi_manager.cpp/h    — Multi-SSID WiFi with timeout per SSID
└── eeprom_buffer.cpp/h   — Circular EEPROM offline sample buffer
```

---

## Relay Auto Cut-off Thresholds

The relay opens (stops charging) when **any** of these conditions is true:

| Condition | Cut-off | Resume |
|---|---|---|
| State of Charge | SoC >= 95 % | SoC <= 85 % |
| Bus Voltage | V >= 4.15 V | V <= 4.05 V |
| Temperature | T >= 45 °C | T <= 40 °C |
| **AH consumed** | **AH >= 2.1 Ah (50 % of 4.2 Ah)** | reset via MQTT `reset_ah` |

All thresholds are defined in `src/config.h` and can be changed without touching any other file.

---

## MQTT

### Broker
- **Host:** `0d34f5789e1e4a669367abfe5bd45b15.s1.eu.hivemq.cloud`
- **Port:** `8883` (TLS)
- **Publish topic:** `battery/data`
- **Subscribe topic:** `battery/recieve`

### Published JSON payload (every 5 s)
```json
{
  "uptime_ms": 123456,
  "bus_V": 3.782,
  "shunt_mV": 12.5,
  "current_A": 0.250,
  "power_W": 0.945,
  "temperature": 27.40,
  "soc_percent": 68.50,
  "soh_percent": 97.20,
  "ah_used": 0.3472,
  "ah_rated": 4.20,
  "ah_percent": 8.3,
  "relay": 0,
  "relay_reason": "None"
}
```
> `relay: 0` = CLOSED (charging ON), `relay: 1` = OPEN (charging OFF)

### Remote commands (send JSON to `battery/recieve`)
```json
{ "command": "relay_on"     }   // force relay closed  (charge ON)
{ "command": "relay_off"    }   // force relay open    (charge OFF)
{ "command": "relay_toggle" }   // toggle current state
{ "command": "relay_auto"   }   // return to automatic threshold control
{ "command": "reset_ah"     }   // reset AH counter to 0
{ "command": "led_toggle"   }   // toggle built-in LED
{ "command": "restart"      }   // reboot ESP32
```

---

## Getting Started

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- ESP32 DevKit board connected via USB

### Build & Upload
```bash
# Build only
platformio run

# Build and flash to board
platformio run --target upload

# Open serial monitor (115200 baud)
platformio device monitor --baud 115200
```

### Configuration
Edit `src/config.h` to change:
- WiFi credentials (up to 3 SSIDs tried in order)
- MQTT broker credentials
- Battery rated capacity (`BATTERY_RATED_AH`)
- AH cut-off percentage (`RELAY_CUTOFF_AH_PERCENT`)
- All relay / temperature / voltage thresholds

---

## Dependencies

| Library | Version |
|---|---|
| knolleary/PubSubClient | ^2.8 |
| adafruit/Adafruit INA219 | ^1.0 |
| adafruit/Adafruit GFX Library | ^1.11 |
| adafruit/Adafruit SSD1306 | ^2.5 |
| bblanchon/ArduinoJson | ^7.0 |

---

## Flash Usage (latest build)

| Resource | Usage |
|---|---|
| **RAM** | 14.3 % (46.8 KB / 320 KB) |
| **Flash** | 72.2 % (946 KB / 1280 KB) |

---

## License

MIT — free to use, modify, and distribute with attribution.
