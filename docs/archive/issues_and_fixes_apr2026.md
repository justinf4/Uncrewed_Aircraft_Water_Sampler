# Issues & Fixes Log — April 2026

Reference notes for problems encountered during initial bring-up of the WS_PIO project.
Use this if similar issues resurface in the future.

---

## 1. GPS fix never triggered file rename

**Symptom:** Board got a GPS signal but `fixAnnounced` never fired, so the log file stayed
as `/nodate.csv` instead of switching to `/YYYYMMDD.csv`.

**Root cause:** The fix condition included `gps.location.age() < 2000`. The `pulseIn` call
(up to 25 ms blocking per loop) combined with the heartbeat's separate `pulseIn` call
(another 25 ms) meant the loop could take up to 50 ms per iteration. On a slow iteration,
the GPS location age could have advanced past 2000 ms between the NMEA read and the check,
causing the condition to silently fail every time.

**Fix:** Removed the `age()` guard from the `fixAnnounced` condition. The check is now:
```cpp
if (!fixAnnounced && gps.location.isValid() && gps.date.isValid())
```
The `age()` guard is still used when snapping location at sample time (`snap.hasFix`) where
freshness actually matters.

---

## 2. Double pulseIn consuming receiver signals

**Symptom:** Occasional missed triggers; heartbeat showed high `rx_pulse` but trigger didn't fire.

**Root cause:** The heartbeat block called `pulseIn(RECEIVER_PIN, HIGH, 25000)` separately
for its display value, then the trigger detection code called `pulseIn` again immediately
after. Two consecutive calls in the same loop iteration meant the second call could time out
(0 µs) if there was only one pulse in that 20 ms window, causing a real trigger signal to
be missed.

**Fix:** Moved to a single `pulseIn` call per loop iteration. The result is stored in
`uint32_t pulse` and reused by both the heartbeat display and the trigger detection:
```cpp
uint32_t pulse     = pulseIn(RECEIVER_PIN, HIGH, 25000);
bool receiverHigh  = (pulse > TRIGGER_THRESHOLD);
// heartbeat uses 'pulse' for display
// trigger detection uses 'receiverHigh'
```

---

## 3. CSV file naming format

**Symptom:** File was named `MMDDYYYY.csv` (e.g., `04172026.csv`). User wanted `YYYYMMDD.csv`
(e.g., `20260417.csv`) for chronological sort order.

**Fix:** Changed sprintf format string from:
```cpp
sprintf(fname, "/%02d%02d%04d.csv", gps.date.month(), gps.date.day(), gps.date.year());
```
to:
```cpp
sprintf(fname, "/%04d%02d%02d.csv", gps.date.year(), gps.date.month(), gps.date.day());
```

---

## 4. GPS date is UTC, not local time — FIXED

**Symptom:** File was named with UTC date (April 18) when local date was April 17. Occurred
any time after 20:00 EDT (8 PM local), since UTC is 4 hours ahead — UTC midnight rolls the
date forward before local midnight.

**Example:** Samples at 22:00 EDT (April 17) → GPS UTC = 02:00 April 18 → old code produced
file `20260418.csv` with correctly-offset time but wrong day.

**Root cause:** `dateStr()` used `gps.date.day/month/year()` directly (UTC), while `estStr()`
did subtract 4 hours from UTC hour but did not propagate the day rollback when the subtraction
crossed midnight.

**Fix:** Replaced the separate `dateStr()` / `estStr()` helpers with a shared `localComponents()`
function that applies `TZ_OFFSET` to the hour, then rolls back day/month/year when the result
goes negative:

```cpp
const int TZ_OFFSET = -4;  // EDT (summer); change to -5 for EST (winter)

bool localComponents(int* yr, int* mo, int* dy, int* hr, int* mn, int* sc) {
  if (!gps.date.isValid() || !gps.time.isValid()) return false;
  *hr = (int)gps.time.hour() + TZ_OFFSET;
  *yr = gps.date.year(); *mo = gps.date.month(); *dy = gps.date.day();
  if (*hr < 0) {
    *hr += 24;
    (*dy)--;
    if (*dy < 1) {
      (*mo)--;
      if (*mo < 1) { *mo = 12; (*yr)--; }
      *dy = daysInMonth(*mo, *yr);  // handles all month lengths + leap years
    }
  }
  return true;
}
```

Both `dateStr()`, `estStr()`, and the GPS fix file-naming block now call `localComponents()`.

**Seasonal reminder:** Change `TZ_OFFSET` from `-4` to `-5` in November when clocks fall back
to EST. The constant is at the top of `src/main.cpp` next to the other config constants.

---

## 5. SD write not confirmed visible to user

**Symptom:** User was unsure whether data was actually written because the serial output
was minimal.

**Fix:** Expanded `writeSDRow` serial output to show everything written:
```
[SD] Wrote row 2 to /nodate.csv | 04/17/2026 21:57:51 | Surf=22.50C Depth=19.25C | LAT=xx.xxx LNG=-xx.xxx
```
Also added distinct messages for file create vs file resume:
```
[SD] Created /20260418.csv          ← new file for today
[SD] Opened /20260418.csv — resuming from row 5   ← existing file
```

---

## 6. LED behavior defined and implemented

**Hardware:** The Arduino Nano ESP32 has an onboard RGB LED. All channels are **active LOW**
(`LOW` = on, `HIGH` = off). Available macros: `LEDG` (pin 15), `LEDR` (pin 14), `LEDB` (pin 16).

> `LED_BUILTIN` = D13 = SPI SCK — **never use this**, it corrupts SD card communication.
> `LED_GREEN` in raw GPIO mode = GPIO0 = same physical pin as reed switch (MAG_PIN).
> Always use the Arduino-mode macros `LEDG`, `LEDR`, `LEDB`.

**Implemented LED behavior:**

| Event | LED | Behavior |
|-------|-----|----------|
| Trigger received | Green | On for 1 second, then off |
| Servo cycle running (modes 1–3) | Red | Flashes every 150 ms |
| Cycle complete (mode 3 → 0) | Red | Solid on for 1 second, then off |

---

## 7. initLogFile — sample counter resume

**How it works:** On startup (and on GPS fix causing a file switch), `initLogFile` opens
the existing CSV file, counts newline characters, and sets `sampleCount = lines - 1`
(subtracting the header). The next trigger increments `sampleCount` before writing,
so rows are numbered consecutively with no gaps or duplicates even across power cycles.

**Example:** SD card has `20260418.csv` with header + 9 data rows = 10 lines.
`sampleCount` is set to 9. Next trigger: `sampleCount++` → 10. Row 10 is appended.

---

## 9. DS18B20 first-reading returns 85°C

**Symptom:** First temperature sample after power-on always reads 85°C for both sensors,
regardless of actual temperature. All subsequent readings are correct.

**Root cause:** The DS18B20 stores `0x0550` (85°C) as its factory default / power-on reset
value in the scratchpad temperature register. This is documented in the Maxim/Analog Devices
DS18B20 datasheet (Table 2 — Scratchpad Memory). The value persists until the first
successful conversion completes. If `getTempC()` is called before the sensor has finished its
first conversion, it returns this sentinel value.

**Fixes applied (two layers):**

Layer 1 — Warmup in `setup()`:
```cpp
// Discard power-on default with two back-to-back conversion cycles
while (ds.selectNext()) ds.getTempC();  // trigger + discard first
delay(800);                              // wait for next conversion (~750ms for 12-bit)
while (ds.selectNext()) ds.getTempC();  // discard second to be safe
```
This runs inside the existing startup window, before the system announces "Ready".
Cost: adds ~1 second to startup time, which is acceptable.

Layer 2 — Runtime guard:
```cpp
if (fabsf(c - 85.0f) < 0.5f) {
    Serial.println("[TEMP] WARN: 85C default detected, discarding");
    c = NAN;  // treated as 0.00 in CSV
}
```
This acts as a safety net. If 85°C is ever seen at sample time, it's discarded and logged
as a warning. The CSV records `0.00` (from NAN handling) rather than a false 85°C reading.

**Sensor model:** DS18B20 by Maxim Integrated (now Analog Devices). See `docs/ds18b20_reference.md`
for full sensor reference documentation.

---

## 10. DFU upload fails: "No DFU capable USB device"

**Symptom:** `upload.bat` fails with `No DFU capable USB device available` from `dfu-util`.

**Root cause:** The Arduino Nano ESP32 uses native USB-OTG (no USB-to-serial chip). Uploading
requires the board to be in DFU bootloader mode. If the board is running application firmware,
DFU mode must be triggered manually or by the upload tool. Sometimes the board drops off USB
entirely after a failed upload.

**Recovery steps (in order):**
1. Close any open serial monitor windows — they hold the COM port
2. Double-tap the RESET button quickly — board LED will change to indicate DFU mode
3. Run `upload.bat` within ~5 seconds of the double-tap
4. If the COM port disappears: press RESET once to re-enumerate in application mode, then retry

**Prevention:** Always close `monitor.bat` before running `upload.bat`. The `upload_and_monitor.bat`
script handles this correctly by design (uploads first, then opens monitor).

---

## 11. Serial monitor misses startup messages (native USB CDC)

**Symptom:** Connecting the serial monitor after the board boots shows nothing until the first
5-second heartbeat. Startup messages (`[SD] Card ready`, `[TEMP] Priming sensors`, etc.) are
never visible.

**Root cause:** The Arduino Nano ESP32 uses native USB CDC (no UART bridge chip). The USB
stack initializes after the 2-second `delay(2000)` in `setup()`. By the time the PC's monitor
connects, the board has already printed and moved on.

**Workaround:** The 5-second heartbeat (`[STATUS]` line) provides current system state even
when startup messages are missed. Press RESET on the board while the monitor is already open
to catch all startup messages in one window.

---

## 12. FILE_APPEND won't create a missing file — no CSV written after SD delete

**Symptom:** User deleted `nodate.csv` from the SD card while the board was running (or between
power cycles where SD init succeeded but the file vanished). Triggering a sample produced no file
on the SD card — no error was visible without a serial monitor open.

**Root cause:** The ESP32 Arduino SD library's `FILE_APPEND` mode silently returns null when the
target file does not exist. The original `writeSDRow` opened in `FILE_APPEND`, checked for null,
printed `[SD] ERROR opening` to serial, and returned — but never attempted to recreate the file.
There was no recovery path.

**Fix:** Added an existence check at the top of `writeSDRow`. If the file is missing, it is
recreated with the CSV header before appending:

```cpp
if (!SD.exists(logFilename)) {
    File hdr = SD.open(logFilename, FILE_WRITE);
    if (hdr) {
        hdr.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C");
        hdr.close();
        Serial.print("[SD] Recreated "); Serial.println(logFilename);
    } else {
        Serial.print("[SD] ERROR recreating "); Serial.println(logFilename);
        return;
    }
}
```

Serial output now distinguishes a recreated file (`[SD] Recreated /nodate.csv`) from the normal
first-boot create path (`[SD] Created /nodate.csv`).

**Note:** `sampleCount` is not reset when the file is auto-recreated mid-run — row numbers
continue from where they were so no data already collected in memory is lost. If a completely
fresh counter is needed, power-cycle the board.

---

## 13. Automatic US Eastern DST detection

**Symptom / Request:** The timezone offset was a hardcoded constant (`TZ_OFFSET = -4` for summer EDT,
`-5` for winter EST), requiring a manual code change and re-upload every spring and fall.

**Fix:** Removed the constant and replaced it with three helper functions that compute the correct
offset automatically from the GPS UTC date and time:

```cpp
// Sakamoto's algorithm — returns 0=Sun … 6=Sat
static int dayOfWeek(int y, int m, int d) { ... }

// Returns day-of-month for the Nth occurrence of weekday in month/year
static int nthWeekday(int n, int dow, int m, int y) { ... }

// Returns -4 (EDT) or -5 (EST)
// DST starts: 2nd Sunday March at UTC 07:00 (= 2:00 AM local before spring-forward)
// DST ends:   1st Sunday November at UTC 06:00 (= 2:00 AM local before fall-back)
static int getOffset(int utcYear, int utcMonth, int utcDay, int utcHour) {
  int dstStart = nthWeekday(2, 0, 3,  utcYear);
  int dstEnd   = nthWeekday(1, 0, 11, utcYear);
  bool afterStart = (utcMonth > 3) ||
                    (utcMonth == 3 && utcDay > dstStart) ||
                    (utcMonth == 3 && utcDay == dstStart && utcHour >= 7);
  bool beforeEnd  = (utcMonth < 11) ||
                    (utcMonth == 11 && utcDay < dstEnd) ||
                    (utcMonth == 11 && utcDay == dstEnd && utcHour < 6);
  return (afterStart && beforeEnd) ? -4 : -5;
}
```

`localComponents()` now calls `getOffset()` with the raw UTC values before applying the offset,
so all derived helpers (`dateStr`, `estStr`, file naming) are automatically correct year-round.

**No user action required** — the board handles DST transitions automatically based on GPS date/time.

---

## 14. TM1637 4-digit 7-segment display — sample counter

**Feature added:** A WWZMDiB TM1637-based 4-digit 7-segment display with center colon shows
sample progress at a glance so syringes can be labeled before collection.

**Display layout:**
```
[ LL : RR ]
  ↑     ↑
  │     └── Second next sample (right syringe to prep)
  └──────── Next sample (left syringe to prep)
```

Both digits always show the **upcoming** samples — not what was just taken.

| State | sampleCount | Display |
|-------|-------------|---------|
| Fresh start | 0 | `01:02` |
| After 1st trigger | 1 | `02:03` |
| After 6th trigger | 6 | `07:08` |
| Resume from SD with 6 rows | 6 | `07:08` |

Formula: `left = sampleCount + 1`, `right = sampleCount + 2` (both `% 100`).

**Hardware:**
- Module: WWZMDiB TM1637 4-digit LED display (Amazon ASIN B0BFQNFX6D)
- Driver IC: TM1637 (2-wire interface: CLK + DIO)
- Voltage: 3.3V compatible
- Pins wired: CLK → D5, DIO → D7

**Wiring (4 pins):**

| Module pin | Board pin |
|------------|-----------|
| VCC        | 3.3V      |
| GND        | GND       |
| CLK        | D5        |
| DIO        | D7        |

**Library:** `TM1637` by Avishay Orpaz — added via `lib_deps = TM1637` in `platformio.ini`.
PlatformIO downloads it automatically on first build.

**Colon:** Always illuminated. Controlled by the `0b01000000` dots mask in `showNumberDecEx()`.

**Update points in firmware:**
1. `setup()` — shows `00:01` immediately on boot (before SD loads)
2. `setup()` — refreshed after `initLogFile()` resumes a count from an existing SD file
3. On trigger — updated immediately after `sampleCount++` (before servo moves)
4. On GPS fix — refreshed after `initLogFile()` switches to the date-named file

**Overflow:** Both sides use `% 100`, so display wraps at 99 (shows `99:00`) rather than
corrupting the 4-digit layout. In practice this should never be reached in a single session.

---

## 8. GPS library SPI conflict with SD card

**Symptom:** GPS and SD card could not operate at the same time. One or both would fail —
likely SD writes corrupting or GPS data lost.

**Root cause:** The original GPS library used the SPI bus to communicate with the GPS module.
The SD card also uses SPI. Both peripherals were competing for the same SPI bus without proper
chip-select coordination, causing bus contention and unreliable operation.

**Fix:** Switched to **TinyGPSPlus** library with a UART-based GPS module. The GPS module
connects to `HardwareSerial(1)` on D3 (RX) and D4 (TX) — completely separate from SPI. The
SD card retains exclusive use of the SPI bus (SCK=D13, MOSI=D11, MISO=D12, CS=D10).

This is the configuration in the current firmware:
```cpp
HardwareSerial gpsSerial(1);
gpsSerial.begin(9600, SERIAL_8N1, D3, D4);
```

**Lesson:** Any peripheral that shares the SPI bus with the SD card needs its own chip-select
line and careful CS management. Using a UART GPS module avoids this entirely.

---

## 15. ESP32PWM::allocateTimer() kills servo on ESP32-S3

**Symptom:** Servos produced no movement at all. `writeMicroseconds()` calls had no effect.
No error was printed — the code compiled and ran silently, but neither servo moved.

**Root cause:** The original servo setup called all four `ESP32PWM::allocateTimer()` overloads:

```cpp
ESP32PWM::allocateTimer(0);
ESP32PWM::allocateTimer(1);
ESP32PWM::allocateTimer(2);
ESP32PWM::allocateTimer(3);
pair1.setPeriodHertz(50);
pair2.setPeriodHertz(50);
pair1.attach(SERVO1_PIN, 500, 2500);
pair2.attach(SERVO2_PIN, 500, 2500);
```

On the ESP32-S3, the Arduino framework (espressif32 / framework-arduinoespressif32) internally
reserves LEDC hardware timers for its own use (USB CDC, peripheral drivers, etc.). Calling
`allocateTimer(0)` through `allocateTimer(3)` pre-claims **all four** hardware LEDC timers
before the framework can take its share. When `attach()` is subsequently called, the LEDC
subsystem has no free timer to assign to the PWM channel — so `attach()` silently fails and
no PWM signal is ever output on the servo pin. The servo receives no pulses and does not move.

This is specific to the ESP32-S3 (Arduino Nano ESP32). On older ESP32 variants the same
`allocateTimer` pattern works fine because fewer timers are pre-used by the framework.

**Fix:** Remove all `allocateTimer` calls and use plain `attach(pin)` without the min/max
microsecond overload. This lets the ESP32Servo library negotiate timer allocation with the
framework at attach time:

```cpp
pair1.setPeriodHertz(50);
pair2.setPeriodHertz(50);
pair1.attach(SERVO1_PIN);
pair2.attach(SERVO2_PIN);
pair1.writeMicroseconds(STOP);
pair2.writeMicroseconds(STOP);
```

**How the root cause was found:** A minimal test sketch (just `setPeriodHertz` + bare `attach` +
`writeMicroseconds` in a loop) moved the servo immediately. Diffing the test sketch against the
full firmware revealed the four `allocateTimer` calls as the only structural difference in the
servo setup block.

**Affected pins:** SERVO1_PIN = D9, SERVO2_PIN = D5. Both fail identically when all four
timers are pre-claimed.

**Note:** Removing `allocateTimer` alone did not fix servo movement. The actual root cause was
the MCPWM pin remap issue documented in Issue #16 below. Both fixes are present in the final
working code.

---

## 16. ESP32Servo MCPWM path ignores Arduino pin remap — servo signal on wrong GPIO

**Symptom:** Servos produced no movement. RC receiver trigger never fired even when the switch
was flipped above the 1800 µs threshold. Serial monitor showed `rx_pulse` stuck near 1510 µs
regardless of switch position.

**Root cause — three-layer interaction:**

1. The local `lib/ESP32Servo/src/ESP32Servo.h` constructs the internal PWM object as
   `ESP32PWM pwm{false}` — `false` = fixed-frequency mode. On ESP32-S3, fixed-frequency mode
   **prefers MCPWM hardware** over LEDC (see `ESP32PWM::allocatenext`, non-variable branch).

2. The Arduino Nano ESP32 board (`-DBOARD_HAS_PIN_REMAP`) uses remapped pin numbers: `D9 = 9`
   (Arduino pin), `D5 = 5` (Arduino pin). The framework's `io_pin_remap.h` provides macros that
   transparently remap calls such as `digitalWrite`, `ledcAttachPin`, `pulseIn` etc. by wrapping
   them with `digitalPinToGPIONumber()`. **`mcpwm_gpio_init` is not in that list.**

3. In `ESP32PWM::attachPin(uint8_t pin)`, when `isMCPWM == true`, the library calls
   `mcpwm_gpio_init(mcpwmUnit, signal, pin)`. This is a raw ESP-IDF call — it receives the
   Arduino pin number (e.g. `9` for D9) and interprets it directly as a GPIO number. Arduino
   pin 9 = **GPIO9 = D6 = RECEIVER_PIN**. Arduino pin 5 = **GPIO5 = D2 = MAG_PIN**.

**Effect on each pin:**
| Servo | Arduino pin | MCPWM lands on | Expected GPIO |
|-------|-------------|----------------|---------------|
| pair1 (SERVO1_PIN=D9) | 9 | GPIO9 = D6 = RC receiver | GPIO18 |
| pair2 (SERVO2_PIN=D5) | 5 | GPIO5 = D2 = Reed switch | GPIO8  |

- The actual servo output pins (GPIO18, GPIO8) received **no PWM signal** — servos never moved.
- The RC receiver input (D6/GPIO9) was driven by the MCPWM servo signal (~1510 µs at 50 Hz),
  so `pulseIn(D6, HIGH, 25000)` read the MCPWM pulse, not the real receiver. `rx_pulse` was
  locked near 1510 µs; `receiverHigh` stayed false; **the trigger never fired**.
- The reed switch input (D2/GPIO5) was similarly driven, causing erratic stop-sensor behavior.

**Why the test sketch worked:** The standalone test sketch used the global Arduino IDE library
(`Documents/Arduino/libraries/ESP32Servo/`) which is an older version without ESP32-S3 MCPWM
support. That version falls back to LEDC for all boards. `ledcAttachPin` IS wrapped by the
remap macro (`#define ledcAttachPin(pin,ch) ledcAttachPin(digitalPinToGPIONumber(pin),ch)` in
`io_pin_remap.h`), so it correctly targeted GPIO18 and GPIO8. The full firmware used the local
library (`lib/ESP32Servo/`) which has the newer MCPWM path and triggered the bug.

**Fix:** One line in `lib/ESP32Servo/src/ESP32PWM.cpp`, wrapping the pin argument:

```cpp
// BEFORE (broken — passes Arduino pin number to ESP-IDF GPIO call):
mcpwm_gpio_init(mcpwmUnit, signal, pin);

// AFTER (fixed — converts to physical GPIO number first):
mcpwm_gpio_init(mcpwmUnit, signal, (int)digitalPinToGPIONumber(pin));
```

`digitalPinToGPIONumber()` is declared in `io_pin_remap.h` (available whenever `Arduino.h` is
included) and on boards without pin remapping it is a no-op macro (`#define digitalPinToGPIONumber(x) (x)`),
so this fix is safe for all ESP32 variants.

**Confirmed working** — April 25 2026. Both servos respond to RC trigger after this fix.

---

## 13. Confirmed working state (as of April 17–18 2026)

- Build: SUCCESS, Flash 12.5%, RAM 9.8% (espressif32 @ 6.13.0, framework-arduinoespressif32)
- Upload: DFU over USB, COM11, Arduino Nano ESP32 (VID 2341 PID 0070)
- Trigger: FrSky X8R aux switch → D6, pulseIn threshold 1800 µs
- SD write: confirmed writing to `/nodate.csv` during indoor testing (no GPS fix)
- Servo alternation: verified — `servo_next` flips 1→2→1 after each complete cycle
- Sample counter: resumes correctly from existing SD file on power-up
- LED feedback: green 1s on trigger, red flash during cycle, red solid 1s on completion
