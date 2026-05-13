#include <ESP32Servo.h>
#include <TinyGPSPlus.h>
#include <DS18B20.h>
#include <SD.h>
#include <SPI.h>

// ── Pins ──────────────────────────────────────
const int SD_CS_PIN    = D10;
const int RECEIVER_PIN = D6;
const int MAG_PIN      = D2;    // Reed switch (GPIO0 via Arduino pin 0)
const int SERVO1_PIN   = D9;
const int SERVO2_PIN   = D5;

// ── Servo timing ──────────────────────────────
const int STOP  = 1510;
const int OPEN  = 1000;
const int CLOSE = 2000;

// ── Timing ────────────────────────────────────
const unsigned long PAUSE_TIME      = 2000;
const unsigned long MAG_IGNORE_TIME = 2000;
const unsigned long MAX_MOVE_TIME   = 20000;
const unsigned long LED_FLASH_MS    = 150;  // red flash interval during servo run

// ── Receiver threshold ────────────────────────
// Aux switch: OFF=~1000µs, ON=~2000µs
const uint32_t TRIGGER_THRESHOLD = 1800;

// ── Timezone (US Eastern, auto DST) ───────────
// No manual constant needed — offset is computed from GPS date each call.

// ── Hardware ──────────────────────────────────
Servo pair1, pair2;
DS18B20 ds(8);
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
const uint8_t SURFACE_ADDR[8] = {40, 156, 235, 111, 0, 0, 0, 114};
const uint8_t DEPTH_ADDR[8]   = {40, 122, 122, 116, 0, 0, 0,  63};

// ── State ─────────────────────────────────────
int    mode        = 0;
int    servoIndex  = 0;
Servo* current     = &pair1;
int    sampleCount = 0;

unsigned long prevMillis    = 0;
unsigned long lastHeartbeat = 0;

// LED state
unsigned long ledGreenOffAt  = 0;   // turn green off at this millis()
unsigned long ledRedOffAt    = 0;   // turn red off at this millis() (end-of-cycle solid)
unsigned long ledFlashNext   = 0;   // next red flash toggle
bool          ledRedState    = false;

bool   lastReceiverHigh = false;
bool   fixAnnounced     = false;
char   logFilename[20]  = "/nodate.csv";

struct { bool valid, hasFix; char date[12]; double lat, lng; } snap;

// ── Helpers ───────────────────────────────────
void feedGPS(unsigned long ms) {
  unsigned long s = millis();
  while (millis() - s < ms) while (gpsSerial.available()) gps.encode(gpsSerial.read());
}

// Days in month — needed for day rollback across month/year boundaries
static int daysInMonth(int m, int y) {
  const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

// Sakamoto's algorithm — returns 0=Sun, 1=Mon, ..., 6=Sat
static int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y--;
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

// Returns day-of-month for the Nth occurrence of weekday dow (0=Sun) in month m, year y
static int nthWeekday(int n, int dow, int m, int y) {
  int first = dayOfWeek(y, m, 1);
  return 1 + ((dow - first + 7) % 7) + (n - 1) * 7;
}

// Returns -4 (EDT) or -5 (EST) based on US Eastern DST rules.
// DST starts: 2nd Sunday March at 2:00 AM local = 7:00 UTC
// DST ends:   1st Sunday November at 2:00 AM local = 6:00 UTC (clocks fall back)
static int getOffset(int utcYear, int utcMonth, int utcDay, int utcHour) {
  int dstStart = nthWeekday(2, 0, 3,  utcYear);  // 2nd Sunday March
  int dstEnd   = nthWeekday(1, 0, 11, utcYear);  // 1st Sunday November
  bool afterStart = (utcMonth > 3) ||
                    (utcMonth == 3 && utcDay > dstStart) ||
                    (utcMonth == 3 && utcDay == dstStart && utcHour >= 7);
  bool beforeEnd  = (utcMonth < 11) ||
                    (utcMonth == 11 && utcDay < dstEnd) ||
                    (utcMonth == 11 && utcDay == dstEnd && utcHour < 6);
  return (afterStart && beforeEnd) ? -4 : -5;
}

// Convert GPS UTC date+time to local components.
// Handles day/month/year rollback when timezone offset crosses midnight.
// Returns false if GPS date or time is not yet valid.
bool localComponents(int* yr, int* mo, int* dy, int* hr, int* mn, int* sc) {
  if (!gps.date.isValid() || !gps.time.isValid()) return false;
  int utcHour = (int)gps.time.hour();
  int offset  = getOffset((int)gps.date.year(), (int)gps.date.month(),
                           (int)gps.date.day(), utcHour);
  *hr = utcHour + offset;
  *mn = (int)gps.time.minute();
  *sc = (int)gps.time.second();
  *yr = (int)gps.date.year();
  *mo = (int)gps.date.month();
  *dy = (int)gps.date.day();
  if (*hr < 0) {
    *hr += 24;
    (*dy)--;
    if (*dy < 1) {
      (*mo)--;
      if (*mo < 1) { *mo = 12; (*yr)--; }
      *dy = daysInMonth(*mo, *yr);
    }
  }
  return true;
}

void dateStr(char* buf) {
  int yr, mo, dy, hr, mn, sc;
  if (localComponents(&yr, &mo, &dy, &hr, &mn, &sc))
    sprintf(buf, "%02d/%02d/%04d", mo, dy, yr);
  else if (gps.date.isValid())  // have date but not time — show UTC date as fallback
    sprintf(buf, "%02d/%02d/%04d", gps.date.month(), gps.date.day(), gps.date.year());
  else
    strcpy(buf, "--/--/----");
}

void estStr(char* buf) {
  int yr, mo, dy, hr, mn, sc;
  if (localComponents(&yr, &mo, &dy, &hr, &mn, &sc))
    sprintf(buf, "%02d:%02d:%02d", hr, mn, sc);
  else
    strcpy(buf, "--:--:--");
}

void logGPS(const char* label) {
  feedGPS(100);
  char d[12], t[10];
  dateStr(d); estStr(t);
  Serial.print("[GPS] "); Serial.print(label);
  Serial.print(" | "); Serial.print(d);
  Serial.print(" "); Serial.print(t); Serial.print(" EST | ");
  if (gps.location.isValid()) {
    Serial.print("LAT=");  Serial.print(gps.location.lat(), 6);
    Serial.print(" LNG="); Serial.print(gps.location.lng(), 6);
    Serial.print(" ALT="); Serial.println(gps.altitude.meters(), 1);
  } else Serial.println("NO FIX");
}

void initLogFile(const char* filename) {
  strcpy(logFilename, filename);
  if (!SD.exists(logFilename)) {
    File f = SD.open(logFilename, FILE_WRITE);
    if (f) {
      f.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C");
      f.close();
      Serial.print("[SD] Created "); Serial.println(logFilename);
    } else {
      Serial.print("[SD] ERROR creating "); Serial.println(logFilename);
    }
    sampleCount = 0;
  } else {
    File f = SD.open(logFilename, FILE_READ);
    int lines = 0;
    if (f) { while (f.available()) { if (f.read() == '\n') lines++; } f.close(); }
    sampleCount = max(0, lines - 1);
    Serial.print("[SD] Opened "); Serial.print(logFilename);
    Serial.print(" — resuming from row "); Serial.println(sampleCount);
  }
}

void writeSDRow(float surfaceTemp, float depthTemp) {
  // FILE_APPEND won't create a missing file — recreate with header if needed
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
  File f = SD.open(logFilename, FILE_APPEND);
  if (!f) {
    Serial.print("[SD] ERROR opening "); Serial.println(logFilename);
    return;
  }
  float st = isnan(surfaceTemp) ? 0.0f : surfaceTemp;
  float dt = isnan(depthTemp)   ? 0.0f : depthTemp;
  char t[10]; estStr(t);

  f.print(sampleCount); f.print(",");
  f.print(snap.date);   f.print(",");
  f.print(t);           f.print(",");
  if (snap.hasFix) { f.print(snap.lat, 6); f.print(","); f.print(snap.lng, 6); }
  else             { f.print("NO FIX");    f.print(","); f.print("NO FIX"); }
  f.print(","); f.print(st, 2);
  f.print(","); f.println(dt, 2);
  f.close();

  Serial.print("[SD] Wrote row "); Serial.print(sampleCount);
  Serial.print(" to "); Serial.print(logFilename);
  Serial.print(" | "); Serial.print(snap.date);
  Serial.print(" "); Serial.print(t);
  Serial.print(" | Surf="); Serial.print(st);
  Serial.print("C Depth="); Serial.print(dt);
  Serial.print("C | ");
  if (snap.hasFix) {
    Serial.print("LAT="); Serial.print(snap.lat, 6);
    Serial.print(" LNG="); Serial.println(snap.lng, 6);
  } else Serial.println("NO FIX");
}

// ── LED helpers (active LOW) ───────────────────
void ledSet(int pin, bool on) { digitalWrite(pin, on ? LOW : HIGH); }

// ── Setup ─────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);

  pair1.setPeriodHertz(50);
  pair2.setPeriodHertz(50);
  pair1.attach(SERVO1_PIN);
  pair2.attach(SERVO2_PIN);
  pair1.writeMicroseconds(STOP);
  pair2.writeMicroseconds(STOP);

  current = &pair1;

  pinMode(RECEIVER_PIN, INPUT);
  pinMode(MAG_PIN,      INPUT_PULLUP);

  pinMode(LEDG, OUTPUT); ledSet(LEDG, false);
  pinMode(LEDR, OUTPUT); ledSet(LEDR, false);
  pinMode(LEDB, OUTPUT); ledSet(LEDB, false);

  SPI.begin(D13, D12, D11, D10);
  if (SD.begin(SD_CS_PIN)) {
    Serial.println("[SD] Card ready");
    initLogFile("/nodate.csv");
  } else {
    Serial.println("[SD] Card FAILED — check wiring and FAT32 format");
  }

  delay(100);
  gpsSerial.begin(9600, SERIAL_8N1, D3, D4);

  // DS18B20 power-on default is 85°C — discard it with a warmup conversion.
  // The sensor needs one full conversion cycle (~750ms) before readings are valid.
  Serial.print("[TEMP] Priming sensors");
  while (ds.selectNext()) ds.getTempC();  // trigger + discard first conversion
  delay(800);                             // wait for next conversion to complete
  while (ds.selectNext()) ds.getTempC();  // discard again to be safe
  Serial.println(" — ready");

  Serial.println("[SYS] Ready — waiting for GPS fix...");
}

// ── Loop ──────────────────────────────────────
void loop() {
  // Feed GPS continuously
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  // GPS fix — switch to date-named file (YYYYMMDD.csv)
  // Only check date validity — no age() guard that can race with pulseIn blocking
  if (!fixAnnounced && gps.location.isValid() && gps.date.isValid()) {
    fixAnnounced = true;
    char t[10]; estStr(t);
    Serial.print("[GPS] Fix acquired | "); Serial.print(t);
    Serial.print(" EST | SAT="); Serial.println(gps.satellites.value());
    int yr, mo, dy, hr, mn, sc;
    char fname[20];
    if (localComponents(&yr, &mo, &dy, &hr, &mn, &sc))
      sprintf(fname, "/%04d%02d%02d.csv", yr, mo, dy);
    else
      sprintf(fname, "/%04d%02d%02d.csv", gps.date.year(), gps.date.month(), gps.date.day());
    if (strcmp(logFilename, fname) != 0) initLogFile(fname);
  }

  // ── Single pulseIn call — reused by heartbeat and trigger detection ──
  uint32_t pulse        = pulseIn(RECEIVER_PIN, HIGH, 25000);
  bool     receiverHigh = (pulse > TRIGGER_THRESHOLD);
  bool     triggered    = (receiverHigh && !lastReceiverHigh && mode == 0);
  lastReceiverHigh = receiverHigh;

  // Heartbeat (mode 0 only, every 5s)
  if (mode == 0 && millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    char t[10]; estStr(t);
    Serial.print("[STATUS] idle | servo_next="); Serial.print(servoIndex + 1);
    Serial.print(" | samples=");  Serial.print(sampleCount);
    Serial.print(" | gps=");      Serial.print(fixAnnounced ? "FIX" : "SEARCHING");
    Serial.print(" | rx_pulse="); Serial.print(pulse);
    Serial.print("us | file=");   Serial.print(logFilename);
    Serial.print(" | time=");     Serial.println(t);
  }

  // Trigger
  if (triggered) {
    current = (servoIndex == 0) ? &pair1 : &pair2;
    mode = 1; prevMillis = millis(); sampleCount++;
    feedGPS(100);
    snap.hasFix = gps.location.isValid();
    snap.lat    = snap.hasFix ? gps.location.lat() : 0.0;
    snap.lng    = snap.hasFix ? gps.location.lng() : 0.0;
    dateStr(snap.date);
    snap.valid  = true;
    ledSet(LEDG, true);
    ledGreenOffAt = millis() + 1000;
    ledFlashNext  = millis();           // start red flash immediately
    Serial.print("[TRIG] Servo "); Serial.print(servoIndex + 1); Serial.println(" — Opening");
    logGPS("Sample");
  }

  // ── LED management ────────────────────────────
  // Green: 1s flash on trigger
  if (ledGreenOffAt && millis() >= ledGreenOffAt) {
    ledSet(LEDG, false);
    ledGreenOffAt = 0;
  }

  // Red: flash during modes 1-3, then solid 1s on completion
  if (mode > 0) {
    if (ledRedOffAt == 0 && millis() >= ledFlashNext) {
      ledRedState = !ledRedState;
      ledSet(LEDR, ledRedState);
      ledFlashNext = millis() + LED_FLASH_MS;
    }
  }
  if (ledRedOffAt && millis() >= ledRedOffAt) {
    ledSet(LEDR, false);
    ledRedOffAt  = 0;
    ledRedState  = false;
  }

  // ── State machine ─────────────────────────────
  switch (mode) {
    case 0:
      pair1.writeMicroseconds(STOP);
      pair2.writeMicroseconds(STOP);
      break;

    case 1:
      current->writeMicroseconds(OPEN);
      if (millis() - prevMillis >= MAG_IGNORE_TIME) {
        Serial.print("[MAG] pin="); Serial.println(digitalRead(MAG_PIN));
        if (digitalRead(MAG_PIN) == LOW || millis() - prevMillis >= MAX_MOVE_TIME) {
          current->writeMicroseconds(STOP);
          bool reedHit = (digitalRead(MAG_PIN) == LOW);
          Serial.println(reedHit ? "[SERVO] Reed hit — rack open, stopped"
                                 : "[SERVO] Timeout — rack open, stopped");
          float surfaceTemp = NAN, depthTemp = NAN;
          while (ds.selectNext()) {
            uint8_t addr[8]; ds.getAddress(addr); float c = ds.getTempC();
            // 85.0°C is the DS18B20 power-on default — never a real reading
            if (fabsf(c - 85.0f) < 0.5f) {
              Serial.println("[TEMP] WARN: 85C default detected, discarding");
              c = NAN;
            }
            if      (memcmp(addr, SURFACE_ADDR, 8) == 0) surfaceTemp = c;
            else if (memcmp(addr, DEPTH_ADDR,   8) == 0) depthTemp   = c;
          }
          Serial.print("[TEMP] Surface="); Serial.print(isnan(surfaceTemp) ? 0.0f : surfaceTemp, 2);
          Serial.print("C  Depth=");       Serial.println(isnan(depthTemp) ? 0.0f : depthTemp,  2);
          if (snap.valid) { writeSDRow(surfaceTemp, depthTemp); snap.valid = false; }
          prevMillis = millis(); mode = 2;
        }
      }
      break;

    case 2:
      current->writeMicroseconds(STOP);
      if (millis() - prevMillis >= PAUSE_TIME) { prevMillis = millis(); mode = 3; }
      break;

    case 3:
      current->writeMicroseconds(CLOSE);
      if (millis() - prevMillis >= MAG_IGNORE_TIME) {
        if (digitalRead(MAG_PIN) == LOW || millis() - prevMillis >= MAX_MOVE_TIME) {
          current->writeMicroseconds(STOP);
          bool reedHit = (digitalRead(MAG_PIN) == LOW);
          Serial.println(reedHit ? "[SERVO] Reed hit — rack closed, stopped"
                                 : "[SERVO] Timeout — rack closed, stopped");
          // End-of-cycle: solid red for 1s then off
          ledSet(LEDR, true);
          ledRedState  = true;
          ledRedOffAt  = millis() + 1000;
          ledFlashNext = 0;
          servoIndex = 1 - servoIndex;
          mode = 0;
        }
      }
      break;
  }
}
