# Board Reference — Arduino Nano ESP32

## Overview

The **Arduino Nano ESP32** is the official Arduino board built on the ESP32-S3 chip.
It is NOT a generic "ESP32 dev board" — it carries Arduino's USB VID (0x2341) and is
designed to work with both the Arduino IDE and PlatformIO using the Arduino framework.

- **Product page:** https://store-usa.arduino.cc/products/nano-esp32
- **USB VID:PID:** `2341:0070` (confirmed on this system)
- **Serial port (this machine):** COM11

---

## Key specs

| Spec | Value |
|------|-------|
| MCU | ESP32-S3 (Xtensa LX7 dual-core, up to 240 MHz) |
| RAM | 512 KB SRAM + 8 MB PSRAM |
| Flash | 16 MB |
| Wi-Fi | 802.11 b/g/n (2.4 GHz) |
| Bluetooth | BLE 5 |
| USB | USB-C, native USB-OTG (no USB-to-serial chip) |
| Logic level | **3.3 V** — pins are NOT 5 V tolerant |
| Form factor | Arduino Nano footprint |

---

## Framework notes

The board supports two programming frameworks:

| Framework | When to use |
|-----------|-------------|
| **Arduino** (`framework = arduino` in platformio.ini) | All standard Arduino libraries, ESP32Servo, SD, etc. — what this project uses |
| **ESP-IDF** | Low-level ESP32 SDK — only needed for advanced tasks not covered by Arduino libraries |

This project uses `framework = arduino` via the `espressif32` PlatformIO platform.
Do not switch to ESP-IDF unless you have a specific reason — it would break all Arduino library compatibility.

---

## PlatformIO config (this project)

```ini
platform = espressif32
board    = arduino_nano_esp32
framework = arduino
upload_port = COM11
monitor_port = COM11
monitor_speed = 115200
```

---

## First-time build results (confirmed working)

- Build: **SUCCESS**
- Flash usage: **12.5%** (393,457 / 3,145,728 bytes)
- RAM usage: **9.8%** (32,220 / 327,680 bytes)
- Warnings: 2 harmless `#undef` warnings from `lib/OneWire/OneWire.cpp` — safe to ignore
- Toolchain installed: `toolchain-xtensa-esp32s3 @ 8.4.0+2021r2-patch5`
- Platform version: `espressif32 @ 6.13.0`

---

## Onboard RGB LED

The board has a built-in RGB LED. All three channels are **active LOW** — `LOW` = on, `HIGH` = off.

| Macro | Alias | Arduino pin | Used for |
|-------|-------|-------------|----------|
| `LED_RED` | `LEDR` | 14 | Available |
| `LED_GREEN` | `LEDG` | 15 | **Trigger confirmation** — flashes 1s on each trigger |
| `LED_BLUE` | `LEDB` | 16 | Available |

> `LED_BUILTIN` = D13 = SPI SCK — **do not use**, conflicts with SD card.
>
> `LED_GREEN` in raw GPIO mode maps to GPIO0, which is also the reed switch (MAG_PIN).
> Always use the `LEDG` / `LED_GREEN` Arduino macro, never `GPIO0` directly for the LED.

## Important hardware notes

- **3.3 V logic only** — never connect 5 V signals directly to any GPIO
- The board enumerates as a USB CDC serial device — no separate USB-to-serial chip
- On Windows, it appears as "USB Serial Device (COMx)" in Device Manager
- If the port disappears during upload, try holding the BOOT button while clicking Upload,
  then release after the upload starts (only needed if auto-reset doesn't work)
- There is a known quirk where some ESP32-S3 boards require a double-tap of the RESET button
  to enter bootloader mode if auto-upload fails
