# Library Reference

Libraries required for this project. SD and SPI are bundled with the ESP32 Arduino core — no download needed.

---

## ESP32Servo

Allows the Arduino Nano ESP32 to control RC servo motors using hardware PWM timers, mimicking the standard Arduino Servo API.

- **Repo:** https://github.com/madhephaestus/ESP32Servo
- **Latest release:** v0.13.0
- **Download zip:** https://github.com/madhephaestus/ESP32Servo/archive/refs/tags/v0.13.0.zip
- **Dependencies:** None
- **Verify latest:** https://github.com/madhephaestus/ESP32Servo/releases/latest

---

## TinyGPSPlus

Parses NMEA sentences from a GPS module over serial and provides easy access to location, date/time, altitude, and satellite count.

- **Repo:** https://github.com/mikalhart/TinyGPSPlus
- **Latest release:** v1.0.3
- **Download zip:** https://github.com/mikalhart/TinyGPSPlus/archive/refs/tags/v1.0.3.zip
- **Dependencies:** None
- **Verify latest:** https://github.com/mikalhart/TinyGPSPlus/releases/latest

---

## DS18B20 (matmunk)

Lightweight DS18B20 one-wire temperature sensor library. Uses a clean iterator-style API (`selectNext()`, `getAddress()`, `getTempC()`) that works without knowing sensor addresses in advance.

- **Repo:** https://github.com/matmunk/DS18B20
- **Latest release:** v2.0.0
- **Download zip:** https://github.com/matmunk/DS18B20/archive/refs/tags/v2.0.0.zip
- **Dependencies:** OneWire (see below)
- **Verify latest:** https://github.com/matmunk/DS18B20/releases/latest

---

## OneWire (dependency of DS18B20)

Low-level 1-Wire bus protocol driver required by the DS18B20 library.

- **Repo:** https://github.com/PaulStoffregen/OneWire
- **Latest release:** v2.3.8
- **Download zip:** https://github.com/PaulStoffregen/OneWire/archive/refs/tags/v2.3.8.zip
- **Dependencies:** None
- **Verify latest:** https://github.com/PaulStoffregen/OneWire/releases/latest

---

## Built-in (no download needed)

| Library | Notes |
|---------|-------|
| `SD`    | Bundled with espressif32 core |
| `SPI`   | Bundled with espressif32 core |

---

## How to install locally (offline)

1. Download each zip from the links above
2. Extract the zip — you will get a folder like `ESP32Servo-0.13.0/`
3. Rename the folder to remove the version suffix: `ESP32Servo/`
4. Place the folder inside `lib/`:
   ```
   lib/
   ├── ESP32Servo/
   ├── TinyGPSPlus/
   ├── DS18B20/
   └── OneWire/
   ```
5. PlatformIO will find them automatically on the next build — no `lib_deps` entry needed

> **Tip:** The zip URL pattern is always `https://github.com/{user}/{repo}/archive/refs/tags/{tag}.zip`
