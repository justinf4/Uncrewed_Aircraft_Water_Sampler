# Pin Reference — Arduino Nano ESP32

## Assigned pins

| Arduino Pin | Function | Notes |
|-------------|----------|-------|
| D3 | GPS UART RX | HardwareSerial 1, 9600 baud |
| D4 | GPS UART TX | HardwareSerial 1, 9600 baud |
| D6 | RC Receiver PWM input | pulseIn; >1700 µs = triggered |
| D8 | Servo 2 (pair2) | Moved from D11 to avoid SPI MOSI conflict |
| D9 | Servo 1 (pair1) | |
| D10 | SD card CS | SPI chip select |
| D11 | SPI MOSI | SD card data out — **do not use for anything else** |
| D12 | SPI MISO | SD card data in |
| D13 | SPI SCK | SD card clock |
| A0 | DS18B20 OneWire bus | Both surface and depth sensors on same bus |
| 0 | Reed switch (MAG_PIN) | INPUT_PULLUP, active LOW — stops servo at end of travel |

## Free pins (unassigned)

D2, D5, D7, A1, A2, A3, A4, A5, A6, A7

## Servo timing constants

| Constant | Value | Meaning |
|----------|-------|---------|
| STOP | 1510 µs | Servo stopped (continuous rotation neutral) |
| OPEN | 1000 µs | Rack extends open |
| CLOSE | 2000 µs | Rack retracts closed |

## RC Receiver signal

- Signal type: PWM (standard RC servo PWM, 50 Hz frame rate)
- Pulse range: ~1000 µs (stick low/off) → ~2000 µs (stick high/on)
- Trigger threshold: **>1700 µs** = triggered
- Detection: `pulseIn(D6, HIGH, 25000)` — 25 ms timeout, non-blocking if no signal

## Servo alternation logic

Each complete open→pause→close cycle toggles `servoIndex` (0↔1).
- `servoIndex = 0` → pair1 (D9) is active
- `servoIndex = 1` → pair2 (D8) is active

The toggle happens at the end of mode 3 (closing complete), so the sequence is:
**Trigger 1** → Servo 1 → **Trigger 2** → Servo 2 → **Trigger 3** → Servo 1 → ...
