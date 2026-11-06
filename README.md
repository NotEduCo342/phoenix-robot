# Phoenix Robot 🤖🔥

**Phoenix Robot** is an interactive, production-grade desktop companion robot firmware built for the **ESP32-S3** using the native **ESP-IDF v5.5** framework.

It features an expressive animated eye engine (**RoboEyes**), 6-DOF IMU motion detection, 22.05 kHz I2S sound effects, haptic vibration feedback, capacitive touch controls, an embedded Cyberpunk Web Control Portal, and 5 interactive robot operational modes.

---

## 🛠️ Hardware Specifications

- **MCU**: ESP32-S3-N16R8 (Xtensa dual-core @ 240 MHz, 16 MB Flash, 8 MB Octal PSRAM)
- **Display**: 1.8" TFT SPI (160×128, ST7735R Driver)
- **IMU**: Bosch BMI160 6-Axis Accel / Gyro (I2C)
- **Audio Amplifier**: MAX98357A I2S Mono DAC Amp (GPIO 5/6/7)
- **Microphone**: INMP441 I2S Digital Mic (GPIO 15/16/17)
- **Touch Sensor**: TTP223 Capacitive Touch Module (GPIO 4)
- **Haptics**: Coin Vibration Motor via NPN Transistor (GPIO 10)
- **Strict Boundary**: GPIO 33 to 37 are strictly reserved for Octal PSRAM.

---

## 🔌 Breadboard Wiring Schematic

```text
========================================================================
                  ESP32-S3-N16R8 FULL WIRING SCHEMATIC
         (WARNING: Do NOT use GPIO 33 to 37 - Reserved for PSRAM)
========================================================================

[ 1. TFT 1.8" SPI (ST7735R 128x160) ]
  ESP32-S3 (GPIO 1)  ---------> TFT (RST / Reset)
  ESP32-S3 (GPIO 2)  ---------> TFT (CS / Chip Select)
  ESP32-S3 (GPIO 3)  ---------> TFT (D/C or A0 / Data-Command)
  ESP32-S3 (GPIO 11) ---------> TFT (DIN / SDA / MOSI)
  ESP32-S3 (GPIO 12) ---------> TFT (CLK / SCK / Clock)
  ESP32-S3 (3V3)     ---------> TFT (VCC)
  ESP32-S3 (3V3)     ---------> TFT (BL / Backlight)
  ESP32-S3 (GND)     ---------> TFT (GND)

[ 2. I2C BUS: BMI160 (6-DOF IMU) ]
  ESP32-S3 (GPIO 8)  ---------> BMI160 (SDA)
  ESP32-S3 (GPIO 9)  ---------> BMI160 (SCL)
  ESP32-S3 (3V3)     ---------> BMI160 (VIN)
  ESP32-S3 (GND)     ---------> BMI160 (GND)

[ 3. TOUCH SENSOR: TTP223 ]
  ESP32-S3 (GPIO 4)  ---------> TTP223 (I/O or SIG)
  ESP32-S3 (3V3)     ---------> TTP223 (VCC)
  ESP32-S3 (GND)     ---------> TTP223 (GND)

[ 4. AUDIO DAC: MAX98357A (I2S) ]
  ESP32-S3 (GPIO 5)  ---------> MAX98357A (BCLK)
  ESP32-S3 (GPIO 6)  ---------> MAX98357A (LRC / WS)
  ESP32-S3 (GPIO 7)  ---------> MAX98357A (DIN)
  ESP32-S3 (5V)      ---------> MAX98357A (VIN)
  ESP32-S3 (GND)     ---------> MAX98357A (GND)
  MAX98357A (SPK+/-) ---------> 4-8 Ohm 3W Speaker

[ 5. COIN VIBRATION MOTOR ]
  ESP32-S3 (GPIO 10) ---------> 1k Resistor -> Base of S8050/2N2222 NPN
  Collector (C)      ---------> Motor (-) / Flyback Diode Anode
  Emitter (E)        ---------> GND
  ESP32-S3 (3V3)     ---------> Motor (+) / Flyback Diode Cathode
```

---

## 🧠 Software Architecture

- **Dual-Core FreeRTOS Segregation**:
  - **Core 0**: Sensor sampling (BMI160 @ 100 Hz), I2S audio playback, haptics, alarm state machine, and Wi-Fi management.
  - **Core 1**: Display rendering (RoboEyes engine @ 50 FPS, chunked SPI DMA).
- **First-Run Onboarding Flow**:
  - Robot halts normal operation and displays `WIFI SETUP` on the screen.
  - Broadcasts `PHOENIX-SETUP` (WPA2: `gamephoenix`, IP: `192.168.4.1`).
  - DNS Captive Portal server responds to all DNS queries on UDP 53.
  - Web portal scans surrounding 2.4 GHz networks in the background and returns cached results with 0ms latency.
  - Upon successful connection to the home router, SoftAP is permanently disabled (pure STA mode), dynamic IP is displayed, and the robot boots.
- **Embedded Web Portal**:
  - Modular frontend located in `components/alarm_clock/web/` (`index.html`, `style.css`, `app.js`).
  - Served directly from flash memory with zero filesystem overhead.
  - REST JSON API (`/api/status`, `/api/scan`, `/api/wifi`, `/api/alarm`).

---

## 🎮 5 Robot Operational Modes

Tap and hold the touch sensor on GPIO 4 for **1.2 seconds** to cycle through modes:

1. **Mode 0: Expressive RoboEyes**:
   - Mood system: Neutral, Happy, Tired, Angry, Curious, Dizzy.
   - Responds to motion and tilt from the BMI160 IMU.
   - Auto-blinking and eye looking animations.
2. **Mode 1: Clock & Alarm**:
   - Digital real-time clock synced via NTP.
   - Solar Hijri (Shamsi / Jalali) date conversion algorithm.
   - Dynamic Wi-Fi signal indicator and alarm countdown card.
   - High-volume alarm sequence with synchronized audio beeps and vibration.
3. **Mode 2: System Telemetry**:
   - Live accelerometer & gyroscope readings.
   - Real-time battery, RSSI %, free heap memory, and IP address.
4. **Mode 3: Chrome Dino Jump Game**:
   - Classic side-scrolling obstacle runner.
   - Touch sensor controls jumps.
   - High score tracking and sound effects.
5. **Mode 4: Gyro Tilt Car Game**:
   - Steer a race car using physical tilt (BMI160 X/Y axis).
   - Dodge oncoming traffic with progressive speed increase.

---

## 🚀 Building & Flashing

This project is configured with **PlatformIO** and **ESP-IDF**.

### Prerequisites
- [PlatformIO Core](https://platformio.org/) installed (`pio`).
- USB Type-C cable connected to the ESP32-S3 USB/UART port.

### Build and Upload
```bash
# Compile firmware
pio run

# Flash to ESP32-S3
pio run -t upload

# Open serial monitor
pio device monitor
```

### Re-bundling Web Assets
Whenever you edit files inside `components/alarm_clock/web/`:
```bash
python3 scripts/build_web.py
```
