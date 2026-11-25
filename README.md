# BotOne (Peerbot) – ESP32-S3 Voice Interface for Fastpeer

BotOne is a compact, ESP32-S3-based hardware voice assistant built for realtors on the **Fastpeer** platform. It connects directly to the Fastpeer Cloud Master Agent over encrypted WebSockets (`wss://`) via 2.4 GHz Wi-Fi or a mobile hotspot—bypassing any mobile app relay—and syncs all CRM reports, timers, and meeting notes in real time across the Fastpeer Web App, Mobile App, and BotOne using your account email.

---

## Features

* **Direct-to-Cloud Architecture:** Streams audio directly to the Fastpeer Master Agent over `wss://` without requiring the Fastpeer Mobile App to stay open.
* **Unified Account Sync:** Binds the WebSocket session to your Fastpeer `user_email` so every voice command updates your Web and Mobile dashboards automatically.
* **Persistent Auth (NVS):** Saves your permanent device token in the ESP32-S3's Non-Volatile Storage (`Preferences.h`), surviving power cycles and reboots.
* **Digital I2S Audio:** Captures and plays back uncompressed **16 kHz, 16-bit mono PCM** audio with zero analog Wi-Fi static.
* **Animated OLED Eyes:** Expressive 0.96" OLED states for **Idle** (blinking & glancing), **Listening** (wide eyes + live audio level bar), **Thinking** (scanning eyes), and **Speaking** (animated mouth).
* **Touch-to-Talk:** Capacitive touch trigger for instant voice capture.

---

## Hardware Requirements

* **Microcontroller:** ESP32-S3-N16R8 (16 MB Flash, 8 MB PSRAM, Dual USB-C)
* **Display:** 0.96" I2C OLED (SSD1306, 128×64, address `0x3C`)
* **Input:** Capacitive Touch Sensor (e.g., TTP223)
* **Microphone:** Digital I2S MEMS Microphone (`INMP441` or `ICS-43434`)
* **Audio Output:** `MAX98357A` I2S 3W Class-D Amplifier + Passive 4Ω 3W Speaker

---

## Wiring Pinout (ESP32-S3-N16R8)

| Component | Module Pin | ESP32-S3 GPIO | Power |
| :--- | :--- | :--- | :--- |
| **0.96" OLED (I2C)** | `SDA` / `SCL` | `GPIO 1` / `GPIO 2` | `3.3V` |
| **Touch Sensor** | `SIG` | `GPIO 3` | `3.3V` |
| **I2S Mic (INMP441 / ICS-43434)** | `SCK` / `WS` / `SD` | `GPIO 42` / `GPIO 41` / `GPIO 40` | **`3.3V` only** |
| **I2S Mic (Channel Select)** | `L/R` | `GND` (Left Channel) | — |
| **I2S Amp (MAX98357A)** | `BCLK` / `LRC` / `DIN` | `GPIO 47` / `GPIO 48` / `GPIO 21` | `5V` |

> **Power Warning:** Always power the I2S microphone from **3.3V** (5V will damage it). Power the MAX98357A amplifier from **5V** for full 3W speaker volume.

---

## Software Setup (Arduino IDE)

### 1. Install Board Support
Install the **ESP32** board package by Espressif Systems in the Arduino Boards Manager and select **ESP32S3 Dev Module**.

### 2. Install Required Libraries
Install the following via the Arduino Library Manager:
* `WebSockets` by Markus Sattler
* `Adafruit SSD1306`
* `Adafruit GFX Library`

### 3. Configure Placeholders
Update the top configuration block in the `.ino` sketch with your network and Fastpeer backend credentials:

```cpp
#define WIFI_SSID "YOUR_WIFI_OR_HOTSPOT_SSID"
#define WIFI_PASS "YOUR_WIFI_OR_HOTSPOT_PASSWORD"

#define WS_HOST "api.fastpeer.ai"
#define WS_PORT 443
#define WS_PATH "/ws/botone"
#define USER_EMAIL "your-email@fastpeer.ai"
#define DEFAULT_DEVICE_TOKEN "YOUR_PERMANENT_DEVICE_TOKEN"
