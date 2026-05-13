# Project Setup & Workflow

## Hardware

| Component | Detail |
|-----------|--------|
| Board | Arduino Nano ESP32 (official Arduino, VID 0x2341 PID 0x0070) |
| MCU | ESP32-S3 |
| Serial port | **COM11** |
| RC Receiver | FrSky X8R (PWM output on D6) |

## Software requirements

- [PlatformIO Core](https://platformio.org/install/cli) — `pio` CLI (already installed)
- Python 3.x with `pyserial` — `pip install pyserial` (already installed)

---

## Normal workflow — use run.bat

Double-click **`run.bat`** in the project root. It shows a menu:

```
==========================================
  WS_PIO  —  Arduino Nano ESP32
  Board port: COM11
==========================================

  1.  Build only
  2.  Upload to board
  3.  Monitor serial output
  4.  Upload + Monitor  (most common)
  5.  Exit
```

**Option 4 is the most common** — it compiles, flashes the board, then opens the monitor. Use this any time you change code.

---

## Individual scripts

All scripts live in `scripts/` and can also be run from the project root:

| Script | What it does |
|--------|-------------|
| `scripts\build.bat` | Compile only — confirms code is valid before uploading |
| `scripts\upload.bat` | Compile + upload to COM11 |
| `scripts\monitor.bat` | Open live serial monitor on COM11 (runs until Ctrl+C) |
| `scripts\upload_and_monitor.bat` | Upload then immediately open monitor |

---

## monitor.py — advanced usage

The monitor script supports optional flags for bounded reads (useful for automated testing):

```
python scripts\monitor.py                              # auto-detect port, run forever
python scripts\monitor.py COM11                        # specify port, run forever
python scripts\monitor.py COM11 115200                 # port + baud, run forever
python scripts\monitor.py COM11 115200 --timeout 10    # stop after 10 seconds
python scripts\monitor.py COM11 115200 --lines 20      # stop after 20 lines
```

---

## What to expect on the serial monitor

### At startup (first few seconds)
```
[SD] Card ready
[SD] /nodate.csv
Ready — waiting for GPS fix...
```

### Every 5 seconds while idle
```
[STATUS] idle | servo_next=1 | samples=9 | gps=SEARCHING | rx_pulse=1501us | time=--:--:--
```

- **servo_next** — which servo fires on the next trigger (alternates 1→2→1→2)
- **samples** — how many samples logged so far (counts up from existing SD data)
- **gps** — `SEARCHING` until outside with clear sky view, then `FIX`
- **rx_pulse** — live PWM reading from FrSky X8R; ~1500µs = neutral, ~2000µs = triggered
- **time** — EST time from GPS (shows `--:--:--` until fix)

### When GPS gets a fix (outdoors)
```
GPS fix | 14:32:10 EST | SAT=8
[SD] /04172026.csv
```
The log file switches from `/nodate.csv` to a date-stamped file automatically.

### When transmitter triggers a sample
```
Signal Received — Servo 1 Opening
[GPS] Sample Location | 04/17/2026 14:32:15 EST | LAT=xx.xxxxxx LNG=-xx.xxxxxx ALT=xx.x
Reed LOW, Rack Open / Servo STOP
[TEMP] Surface: 22.50 C | Depth: 19.25 C
[SD] Row 10
[STATUS] idle | servo_next=2 | samples=10 | ...   ← servo_next flipped to 2
```

---

## Reading the SD card data

The SD card creates one `.csv` file per day named `MMDDYYYY.csv` (e.g., `04172026.csv`).
Before GPS fix, data goes to `nodate.csv`.

Columns: `Count, Date, Time_est, Lat, Long, SurfaceTemp_c, DepthTemp_c`

To read: eject the SD card, insert into a computer, open the file in Excel or any spreadsheet app.

---

## Project structure

```
WS_PIO/
├── run.bat                 ← START HERE — main menu launcher
├── platformio.ini          ← board + build config (COM11, arduino_nano_esp32)
├── src/
│   └── main.cpp            ← combined firmware source
├── lib/                    ← local libraries (zips + extracted folders)
│   ├── ESP32Servo/
│   ├── TinyGPSPlus/
│   ├── DS18B20/
│   └── OneWire/
├── scripts/
│   ├── build.bat
│   ├── upload.bat
│   ├── monitor.bat
│   ├── upload_and_monitor.bat
│   └── monitor.py
└── docs/
    ├── setup.md            ← this file
    ├── board_reference.md  ← board specs, framework notes, build results
    ├── libraries.md        ← library sources + offline download links
    └── pin_reference.md    ← all pin assignments + servo timing
```

---

## First-time setup note

The first build downloaded ~400 MB of ESP32 toolchain and Arduino framework — that's a one-time cost. Subsequent builds only recompile changed files and take ~5 seconds.

---

## Adding or updating a library offline

1. Download the zip from `docs/libraries.md`
2. Extract into `lib/` — result should be `lib/LibraryName/` with source files inside
3. Delete the old `lib/LibraryName/` folder first if updating
4. Build with `scripts\build.bat` — PlatformIO finds it automatically, no config change needed
