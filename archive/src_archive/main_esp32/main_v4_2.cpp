#include <ESP32Servo.h>
#include <TinyGPSPlus.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SD.h>
#include <SPI.h>

// ── Pins ──────────────────────────────────────
const int SD_CS_PIN    = D10;
const int RECEIVER_PIN = D6;

const int S1_PIN = D9;
const int S2_PIN = D7;
const int S3_PIN = D5;
const int S4_PIN = D2;

const int REED1  = A4;
const int REED2  = A5;
const int REED3  = A7;
const int REED4  = A6;

// ── Servo timing ──────────────────────────────
const int STOP  = 1510;
const int OPEN  = 1000;
const int CLOSE = 2000;

// ── Timing ────────────────────────────────────
const unsigned long PAUSE_TIME      = 2000;
const unsigned long MAG_IGNORE_TIME = 2000;
const unsigned long MAX_MOVE_TIME   = 10000;
const unsigned long LED_FLASH_MS    = 150;
const uint32_t      TRIGGER_THRESHOLD = 1800;

// ── Hardware ──────────────────────────────────
Servo s1, s2, s3, s4;

OneWire oneWire(17);
DallasTemperature sensors(&oneWire);
DeviceAddress SURFACE_ADDR = { 0x28, 0x9C, 0xEB, 0x6F, 0x00, 0x00, 0x00, 0x72 };
DeviceAddress DEPTH_ADDR   = { 0x28, 0x7A, 0x7A, 0x74, 0x00, 0x00, 0x00, 0x3F };

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

// ── Servo pair structure ───────────────────────
struct ServoPair {
  Servo* a; int pinA; int reedA;
  Servo* b; int pinB; int reedB;
};

ServoPair pairs[2] = {
  { &s1, S1_PIN, REED1, &s2, S2_PIN, REED2 },
  { &s3, S3_PIN, REED3, &s4, S4_PIN, REED4 }
};

// ── State ─────────────────────────────────────
int  mode        = 0;
int  servoIndex  = 0;
int  sampleCount = 0;
bool stoppedA    = false;
bool stoppedB    = false;
bool modeEntered = false;
unsigned long brakeStart  = 0;
unsigned long prevMillis  = 0;
unsigned long lastHeartbeat = 0;

unsigned long ledGreenOffAt = 0;
unsigned long ledRedOffAt   = 0;
unsigned long ledFlashNext  = 0;
bool          ledRedState   = false;

bool lastReceiverHigh = false;
bool fixAnnounced     = false;
char logFilename[20]  = "/nodate.csv";

struct { bool valid, hasFix; char date[12]; double lat, lng; } snap;

// ── Servo helpers ─────────────────────────────
void attachServo(Servo& s, int pin, int cmd) {
  s.attach(pin, 1000, 2000);
  s.writeMicroseconds(cmd);
}

void cutServo(Servo& s, int pin) {
  s.writeMicroseconds(STOP);
  s.detach();
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void startPair(ServoPair& p, int cmd) {
  attachServo(*p.a, p.pinA, cmd);
  delay(50);
  attachServo(*p.b, p.pinB, cmd);
  stoppedA = false;
  stoppedB = false;
  brakeStart = 0;
  prevMillis = millis();
  modeEntered = true;
}

// ── GPS/SD helpers ────────────────────────────
void feedGPS(unsigned long ms) {
  unsigned long s = millis();
  while (millis() - s < ms) while (gpsSerial.available()) gps.encode(gpsSerial.read());
}

static int daysInMonth(int m, int y) {
  const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

static int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y--;
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static int nthWeekday(int n, int dow, int m, int y) {
  int first = dayOfWeek(y, m, 1);
  return 1 + ((dow - first + 7) % 7) + (n - 1) * 7;
}

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
    *hr += 24; (*dy)--;
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
  else if (gps.date.isValid())
    sprintf(buf, "%02d/%02d/%04d", gps.date.month(), gps.date.day(), gps.date.year());
  else strcpy(buf, "--/--/----");
}

void estStr(char* buf) {
  int yr, mo, dy, hr, mn, sc;
  if (localComponents(&yr, &mo, &dy, &hr, &mn, &sc))
    sprintf(buf, "%02d:%02d:%02d", hr, mn, sc);
  else strcpy(buf, "--:--:--");
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
    if (f) { f.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C"); f.close(); }
    sampleCount = 0;
  } else {
    File f = SD.open(logFilename, FILE_READ);
    int lines = 0;
    if (f) { while (f.available()) { if (f.read() == '\n') lines++; } f.close(); }
    sampleCount = max(0, lines - 1);
  }
  Serial.print("[SD] "); Serial.println(logFilename);
}

void writeSDRow(float surfaceTemp, float depthTemp) {
  if (!SD.exists(logFilename)) {
    File hdr = SD.open(logFilename, FILE_WRITE);
    if (hdr) { hdr.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C"); hdr.close(); }
    else { Serial.println("[SD] ERROR"); return; }
  }
  File f = SD.open(logFilename, FILE_APPEND);
  if (!f) { Serial.println("[SD] ERROR"); return; }
  float st = isnan(surfaceTemp) ? 0.0f : surfaceTemp;
  float dt = isnan(depthTemp)   ? 0.0f : depthTemp;
  char t[10]; estStr(t);
  f.print(sampleCount); f.print(",");
  f.print(snap.date);   f.print(",");
  f.print(t);           f.print(",");
  if (snap.hasFix) { f.print(snap.lat, 6); f.print(","); f.print(snap.lng, 6); }
  else             { f.print("NO FIX,NO FIX"); }
  f.print(","); f.print(st, 2);
  f.print(","); f.println(dt, 2);
  f.close();
  Serial.print("[SD] Wrote row "); Serial.println(sampleCount);
}

void ledSet(int pin, bool on) { digitalWrite(pin, on ? LOW : HIGH); }

// ── Setup ─────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);

  // Init all servo pins LOW — same state as after cutServo()
  pinMode(S1_PIN, OUTPUT); digitalWrite(S1_PIN, LOW);
  pinMode(S2_PIN, OUTPUT); digitalWrite(S2_PIN, LOW);
  pinMode(S3_PIN, OUTPUT); digitalWrite(S3_PIN, LOW);
  pinMode(S4_PIN, OUTPUT); digitalWrite(S4_PIN, LOW);

  pinMode(RECEIVER_PIN, INPUT);
  pinMode(REED1, INPUT_PULLUP);
  pinMode(REED2, INPUT_PULLUP);
  pinMode(REED3, INPUT_PULLUP);
  pinMode(REED4, INPUT_PULLUP);

  pinMode(LEDG, OUTPUT); ledSet(LEDG, false);
  pinMode(LEDR, OUTPUT); ledSet(LEDR, false);
  pinMode(LEDB, OUTPUT); ledSet(LEDB, false);

  SPI.begin(D13, D12, D11, D10);
  if (SD.begin(SD_CS_PIN)) {
    Serial.println("[SD] Card ready");
    initLogFile("/nodate.csv");
  } else {
    Serial.println("[SD] Card FAILED");
  }

  delay(100);
  gpsSerial.begin(9600, SERIAL_8N1, D3, D4);

  // ── Temp sensor init ──────────────────────────
  Serial.print("[TEMP] Priming sensors");
  sensors.begin();
  sensors.setResolution(SURFACE_ADDR, 12);
  sensors.setResolution(DEPTH_ADDR, 12);
  sensors.requestTemperatures();   // throwaway read — clears 85C scratchpad value
  delay(800);
  Serial.println(" — ready");

  Serial.println("[SYS] Ready");
}

// ── Loop ──────────────────────────────────────
void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  if (!fixAnnounced && gps.location.isValid() && gps.date.isValid()) {
    fixAnnounced = true;
    int yr, mo, dy, hr, mn, sc;
    char fname[20];
    if (localComponents(&yr, &mo, &dy, &hr, &mn, &sc))
      sprintf(fname, "/%04d%02d%02d.csv", yr, mo, dy);
    else
      sprintf(fname, "/%04d%02d%02d.csv", gps.date.year(), gps.date.month(), gps.date.day());
    if (strcmp(logFilename, fname) != 0) initLogFile(fname);
  }

  bool triggered = false;
  if (mode == 0) {
    uint32_t pulse        = pulseIn(RECEIVER_PIN, HIGH, 25000);
    bool     receiverHigh = (pulse > TRIGGER_THRESHOLD);
    triggered             = (receiverHigh && !lastReceiverHigh);
    lastReceiverHigh      = receiverHigh;
  }

  if (mode == 0 && millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    char t[10]; estStr(t);
    Serial.print("[STATUS] idle | next_pair="); Serial.print(servoIndex + 1);
    Serial.print(" | samples="); Serial.print(sampleCount);
    Serial.print(" | gps="); Serial.print(fixAnnounced ? "FIX" : "SEARCHING");
    Serial.print(" | time="); Serial.println(t);
  }

  if (triggered) {
    mode = 1; modeEntered = false; sampleCount++;
    feedGPS(100);
    snap.hasFix = gps.location.isValid();
    snap.lat    = snap.hasFix ? gps.location.lat() : 0.0;
    snap.lng    = snap.hasFix ? gps.location.lng() : 0.0;
    dateStr(snap.date);
    snap.valid  = true;
    ledSet(LEDG, true);
    ledGreenOffAt = millis() + 1000;
    ledFlashNext  = millis();
    Serial.print("[TRIG] Pair "); Serial.print(servoIndex + 1); Serial.println(" — Opening");
    logGPS("Sample");
  }

  // ── LED ───────────────────────────────────────
  if (ledGreenOffAt && millis() >= ledGreenOffAt) { ledSet(LEDG, false); ledGreenOffAt = 0; }
  if (mode > 0 && ledRedOffAt == 0 && millis() >= ledFlashNext) {
    ledRedState = !ledRedState;
    ledSet(LEDR, ledRedState);
    ledFlashNext = millis() + LED_FLASH_MS;
  }
  if (ledRedOffAt && millis() >= ledRedOffAt) {
    ledSet(LEDR, false); ledRedOffAt = 0; ledRedState = false;
  }

  // ── State machine ─────────────────────────────
  ServoPair& p = pairs[servoIndex];

  switch (mode) {

    case 0:
      break;

    case 1: // Opening
      if (!modeEntered) startPair(p, OPEN);

      if (millis() - prevMillis >= MAG_IGNORE_TIME) {
        if (!stoppedA && (digitalRead(p.reedA) == LOW || millis() - prevMillis >= MAX_MOVE_TIME)) {
          p.a->writeMicroseconds(STOP);
          stoppedA = true;
          Serial.println("[SERVO] A open — stopped");
        }
        if (!stoppedB && (digitalRead(p.reedB) == LOW || millis() - prevMillis >= MAX_MOVE_TIME)) {
          p.b->writeMicroseconds(STOP);
          stoppedB = true;
          Serial.println("[SERVO] B open — stopped");
        }
        if (stoppedA && stoppedB) {
          if (!brakeStart) brakeStart = millis();
          if (millis() - brakeStart >= 1500) {
            cutServo(*p.a, p.pinA);
            cutServo(*p.b, p.pinB);

            // ── Read temperatures ──────────────────
            sensors.requestTemperatures();
            float surfaceTemp = sensors.getTempC(SURFACE_ADDR);
            float depthTemp   = sensors.getTempC(DEPTH_ADDR);
            if (fabsf(surfaceTemp - 85.0f) < 0.5f || surfaceTemp == DEVICE_DISCONNECTED_C) surfaceTemp = NAN;
            if (fabsf(depthTemp   - 85.0f) < 0.5f || depthTemp   == DEVICE_DISCONNECTED_C) depthTemp   = NAN;

            Serial.print("[TEMP] Surface="); Serial.print(isnan(surfaceTemp) ? 0.0f : surfaceTemp, 2);
            Serial.print("C  Depth=");       Serial.println(isnan(depthTemp) ? 0.0f : depthTemp,  2);
            if (snap.valid) { writeSDRow(surfaceTemp, depthTemp); snap.valid = false; }

            prevMillis = millis(); mode = 2; modeEntered = false;
          }
        }
      }
      break;

    case 2: // Pause
      if (millis() - prevMillis >= PAUSE_TIME) {
        prevMillis = millis(); mode = 3; modeEntered = false;
      }
      break;

    case 3: // Closing
      if (!modeEntered) startPair(p, CLOSE);

      if (millis() - prevMillis >= MAG_IGNORE_TIME) {
        if (!stoppedA && (digitalRead(p.reedA) == LOW || millis() - prevMillis >= MAX_MOVE_TIME)) {
          p.a->writeMicroseconds(STOP);
          stoppedA = true;
          Serial.println("[SERVO] A closed — stopped");
        }
        if (!stoppedB && (digitalRead(p.reedB) == LOW || millis() - prevMillis >= MAX_MOVE_TIME)) {
          p.b->writeMicroseconds(STOP);
          stoppedB = true;
          Serial.println("[SERVO] B closed — stopped");
        }
        if (stoppedA && stoppedB) {
          if (!brakeStart) brakeStart = millis();
          if (millis() - brakeStart >= 1500) {
            cutServo(*p.a, p.pinA);
            cutServo(*p.b, p.pinB);
            ledSet(LEDR, true);
            ledRedState  = true;
            ledRedOffAt  = millis() + 1000;
            ledFlashNext = 0;
            servoIndex   = 1 - servoIndex;
            mode = 0; modeEntered = false;
            Serial.println("[SERVO] Cycle complete");
          }
        }
      }
      break;
  }
}