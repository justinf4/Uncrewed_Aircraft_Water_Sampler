# Operating Procedure

Step-by-step guide from power-on to data retrieval. No prior setup needed after the first time.

---

## Scenario A — Deploy in the field (code already on board)

Use this every time after the initial setup is done.

### 1. Power on the board

- Connect the Arduino Nano ESP32 to power (USB or field power supply)
- The board starts running immediately — no button press needed
- Both servos will hold their stopped position

### 2. Connect to serial monitor (optional but recommended for first few minutes)

If you have a laptop with you:
```
Double-click run.bat → Option 3 (Monitor)
```
You should see within a few seconds:
```
[STATUS] idle | servo_next=1 | samples=N | gps=SEARCHING | rx_pulse=~1500us | time=--:--:--
```
- `rx_pulse=~1500us` confirms the receiver is connected and the transmitter is on
- `gps=SEARCHING` is normal — GPS needs 1–3 minutes outdoors to get a fix

### 3. Wait for GPS fix

Keep the system stationary outdoors with a clear view of the sky.
Once locked, the monitor will show:
```
GPS fix | HH:MM:SS EST | SAT=N
[SD] /MMDDYYYY.csv
```
- The log file switches from `nodate.csv` to a date-stamped file (e.g., `04172026.csv`)
- You can now disconnect the laptop — the board logs everything to the SD card on its own

> **No laptop?** The board still works and logs fine without a monitor connected. GPS fix is indicated by the onboard LED behavior if applicable.

### 4. Take a sample — trigger with the transmitter

- Point the transmitter at the receiver
- Move the trigger channel stick (or switch) past center toward full throw
- The board will:
  1. Extend the rack (Servo 1 or 2 opens)
  2. Wait for the reed switch to detect end of travel (or 2-second timeout)
  3. Pause 2 seconds at open position (sample collection window)
  4. Retract the rack back to closed position
  5. Log GPS location + temperature to the SD card
  6. Return to idle, ready for the next trigger

On the monitor you will see:
```
[TRIG] Servo 1 — Opening
[GPS] Sample | 04/17/2026 14:32:15 EST | LAT=xx.xxxxxx LNG=-xx.xxxxxx ALT=xx.x
[SERVO] Reed hit — rack open, stopped
[TEMP] Surface=22.50C  Depth=19.25C
[SD] Wrote row 10 to /20260417.csv | 04/17/2026 14:32:15 | Surf=22.50C Depth=19.25C | LAT=xx.xxx LNG=-xx.xxx
[SERVO] Reed hit — rack closed, stopped
[STATUS] idle | servo_next=2 | samples=10 | ...
```

### 5. Alternate servos

Each trigger alternates between Servo 1 and Servo 2:
- Trigger 1 → Servo 1
- Trigger 2 → Servo 2
- Trigger 3 → Servo 1
- ...and so on

`servo_next=` in the status line always tells you which one fires next.

### 6. Repeat as needed

Trigger as many samples as needed. The board logs every sample to the SD card automatically.

### 7. Power down

- Wait for the rack to fully close after the last sample (listen/watch for servo to stop)
- **Do not cut power while a cycle is in progress** — let it return to idle first
- The status line will show `idle` when it is safe to power down
- Disconnect power

### 8. Read the data

1. Remove the SD card from the board
2. Insert into a computer (SD card reader or laptop slot)
3. Open the `.csv` file named `MMDDYYYY.csv` for today's date (e.g., `04172026.csv`)
   - If there was no GPS fix before sampling, data is in `nodate.csv` instead
4. Open in Excel, Google Sheets, or any spreadsheet app
5. Columns: `Count, Date, Time_est, Lat, Long, SurfaceTemp_c, DepthTemp_c`

---

## Scenario B — Upload updated code then deploy

Use this when code has been changed and needs to be flashed before going out.

### 1. Plug the board into the laptop via USB-C

- Board appears on **COM11**
- Confirm in Device Manager if unsure: look for "USB Serial Device"

### 2. Upload and verify

```
Double-click run.bat → Option 4 (Upload + Monitor)
```

This compiles, uploads, and immediately opens the monitor. Watch for:
```
[SD] Card ready
[SD] /nodate.csv
Ready — waiting for GPS fix...
```
and within 5 seconds:
```
[STATUS] idle | servo_next=1 | samples=N | gps=SEARCHING | rx_pulse=~1500us
```

- If `rx_pulse=0` — transmitter is off or receiver not wired to D6
- If `[SD] Card FAILED` — SD card not seated or wiring issue on SPI pins
- If `rx_pulse` never appears and the monitor is blank — press the RESET button on the board

### 3. Close the monitor and disconnect

- Press **Ctrl+C** in the monitor window to stop monitoring
- Unplug the USB cable
- The board is now ready to deploy on field power

### 4. Continue from Scenario A, Step 1

---

## Quick reference — serial output codes

| Output | Meaning |
|--------|---------|
| `[STATUS] idle \| servo_next=1` | Waiting for trigger, Servo 1 fires next |
| `[STATUS] idle \| servo_next=2` | Waiting for trigger, Servo 2 fires next |
| `[STATUS] ... \| file=/nodate.csv` | No GPS fix yet — file will rename once outdoors |
| `[STATUS] ... \| file=/20260418.csv` | GPS fix acquired, logging to date-named file |
| `gps=SEARCHING` | No GPS position fix — normal indoors |
| `gps=FIX` | GPS locked — location logged with each sample |
| `rx_pulse=~1000us` | Receiver switch in OFF position — normal at rest |
| `rx_pulse=~2000us` | Receiver switch in ON position — above trigger threshold |
| `rx_pulse=0` | No signal — transmitter off or not bound to X8R |
| `[TRIG] Servo N — Opening` | Trigger detected, green LED on, cycle starting |
| `[SERVO] Reed hit — rack open` | Reed switch hit — rack at open position |
| `[SERVO] Timeout — rack open` | Reed switch not detected in 2s — stopped by timeout |
| `[TEMP] Surface=X.XXC  Depth=X.XXC` | Temperature reading from both sensors |
| `[SD] Wrote row N to /file.csv` | Row successfully written — shows full data inline |
| `[SD] Created /file.csv` | New file created for today |
| `[SD] Opened /file.csv — resuming from row N` | Existing file found, continuing from row N |
| `[SERVO] Reed hit — rack closed` | Rack returned to closed — cycle complete, red LED solid 1s |
| `[SD] Card FAILED` | SD card not detected — check wiring/seating, verify FAT32 format |
| `[GPS] Fix acquired` | GPS fix — file will switch to date-named CSV |

---

## Troubleshooting

**Servo doesn't move on trigger**
- Check `rx_pulse` in the status line — should jump above 1700µs when you move the stick
- If it stays at ~1500µs, the wrong channel may be wired or the stick throw is too small
- Try moving the stick all the way to full throw

**Reed switch never triggers (always hits timeout)**
- The rack may not be reaching the magnet
- Timeout of 2 seconds is the fallback — the servo still stops and the cycle completes

**SD Card FAILED at startup**
- Re-seat the SD card
- Check SPI wiring: SCK=D13, MISO=D12, MOSI=D11, CS=D10
- Verify SD card is formatted as FAT32

**No serial output after upload**
- The board boots before the monitor connects — normal
- Wait up to 5 seconds for the first `[STATUS]` heartbeat line to appear
- If nothing after 10s, press the RESET button on the board
