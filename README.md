# HAB Water Sampler — ESP32 Firmware

The firmware runs on an Arduino Nano ESP32 (ESP32-S3), controlling alternating servos via a FrSky X8R 2.4 GHz RC receiver to operate a water sampler for HAB environmental monitoring. A reed switch gives positional servo feedback, while two DS18B20 sensors and a GPS module log timestamped temperature and geospatial data to an SD card each cycle.

## Main Code

The primary firmware file is [`src/main_driver_v6.cpp`](src/main_driver_v6.cpp).

## Hardware

| Component | Details |
|---|---|
| Microcontroller | Arduino Nano ESP32 (ESP32-S3) |
| RC Receiver | FrSky X8R 2.4 GHz |
| Servos | Two alternating servo pairs |
| Position Sensor | Magnetic reed switches |
| Temperature | 2x DS18B20 one-wire sensors  |
| GPS | UART GPS module — TinyGPS+  |
| Storage | SD card over SPI  |

## Project Structure

```
src/main_driver_v6.cpp        ← main firmware
lib/                ← local libraries (ESP32Servo)
docs/       ← issues and fixes log
platformio.ini      ← board and port configuration
```

## How to Open

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
2. Open this folder in VS Code (**File → Open Folder**)
3. PlatformIO will automatically detect the project
4. Use the PlatformIO toolbar to **Build** or **Upload**
