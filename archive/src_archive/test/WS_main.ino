#include <ESP32Servo.h>
#include <TinyGPSPlus.h>
#include <DS18B20.h>
#include <SD.h>
#include <SPI.h>

// ── Pins & constants ──────────────────────────
const int SD_CS_PIN = D10, BUTTON_PIN = D5, MAG_PIN = 0;
const int STOP = 1510, OPEN = 1000, CLOSE = 2000;
const unsigned long PAUSE_TIME = 2000, MAG_IGNORE_TIME = 2000, MAX_MOVE_TIME = 2000;

// ── Hardware ──────────────────────────────────
Servo pair1, pair2;
DS18B20 ds(A0);
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

const uint8_t SURFACE_ADDR[8] = {40, 156, 235, 111, 0, 0, 0, 114};
const uint8_t DEPTH_ADDR[8]   = {40, 122, 122, 116, 0, 0, 0,  63};

// ── State ─────────────────────────────────────
int mode = 0, sampleCount = 0;
unsigned long prevMillis = 0;
bool lastBtn = HIGH, fixAnnounced = false;
char logFilename[20] = "/nodate.csv";

struct { bool valid, hasFix; char date[12]; double lat, lng; } snap;

// ── Helpers ───────────────────────────────────
void feedGPS(unsigned long ms) {
  unsigned long s = millis();
  while (millis() - s < ms) while (gpsSerial.available()) gps.encode(gpsSerial.read());
}

void dateStr(char* buf) {
  if (gps.date.isValid()) sprintf(buf, "%02d/%02d/%04d", gps.date.month(), gps.date.day(), gps.date.year());
  else strcpy(buf, "--/--/----");
}

void estStr(char* buf) {
  if (!gps.time.isValid()) { strcpy(buf, "--:--:--"); return; }
  int h = (int)gps.time.hour() - 4;
  if (h < 0) h += 24;
  sprintf(buf, "%02d:%02d:%02d", h, gps.time.minute(), gps.time.second());
}

void logGPS(const char* label) {
  feedGPS(100);
  char d[12], t[10];
  dateStr(d); estStr(t);
  Serial.print("[GPS] "); Serial.print(label);
  Serial.print(" | "); Serial.print(d);
  Serial.print(" "); Serial.print(t); Serial.print(" EST | ");
  if (gps.location.isValid() && gps.location.age() < 2000) {
    Serial.print("LAT=");  Serial.print(gps.location.lat(), 6);
    Serial.print(" LNG="); Serial.print(gps.location.lng(), 6);
    Serial.print(" ALT="); Serial.println(gps.altitude.meters(), 1);
  } else Serial.println("NO FIX");
}

void initLogFile(const char* filename) {
  strcpy(logFilename, filename);
  if (!SD.exists(logFilename)) {
    File f = SD.open(logFilename, FILE_WRITE);
    if (f) { f.println("Count,Date,Time_est,Lat,Long,SurfaceTemp_c,DepthTemp_c"); f.close(); }
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
  File f = SD.open(logFilename, FILE_APPEND);
  if (!f) { Serial.println("[SD] ERROR"); return; }
  f.print(sampleCount); f.print(",");
  f.print(snap.date);   f.print(",");
  // write EST time directly
  char t[10]; estStr(t);
  f.print(t); f.print(",");
  if (snap.hasFix) { f.print(snap.lat, 6); f.print(","); f.print(snap.lng, 6); }
  else f.print("NO FIX,NO FIX");
  f.print(","); f.print(isnan(surfaceTemp) ? 0.0f : surfaceTemp, 2);
  f.print(","); f.println(isnan(depthTemp)  ? 0.0f : depthTemp,  2);
  f.close();
  Serial.print("[SD] Row "); Serial.println(sampleCount);
}

// ── Setup ─────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);

  pair1.attach(D9); pair2.attach(D11);
  pair1.writeMicroseconds(STOP); pair2.writeMicroseconds(STOP);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MAG_PIN,    INPUT_PULLUP);

  SPI.begin(D13, D12, D11, D10);
  if (SD.begin(SD_CS_PIN)) { Serial.println("[SD] Card ready"); initLogFile("/nodate.csv"); }
  else Serial.println("[SD] Card FAILED");

  delay(100);
  gpsSerial.begin(9600, SERIAL_8N1, D3, D4);
  Serial.println("Ready — waiting for GPS fix...");
}

// ── Loop ──────────────────────────────────────
void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  if (!fixAnnounced && gps.location.isValid() && gps.location.age() < 2000 && gps.date.isValid()) {
    fixAnnounced = true;
    char t[10]; estStr(t);
    Serial.print("GPS fix | "); Serial.print(t);
    Serial.print(" EST | SAT="); Serial.println(gps.satellites.value());
    char fname[20];
    if (gps.date.isValid()) sprintf(fname, "/%02d%02d%04d.csv", gps.date.month(), gps.date.day(), gps.date.year());
    else strcpy(fname, "/nodate.csv");
    if (strcmp(logFilename, fname) != 0) initLogFile(fname);
  }

  bool btn = digitalRead(BUTTON_PIN);
  if (btn == LOW && lastBtn == HIGH) {
    delay(1);
    if (digitalRead(BUTTON_PIN) == LOW && mode == 0) {
      mode = 1; prevMillis = millis(); sampleCount++;
      feedGPS(100);
      snap.hasFix = gps.location.isValid() && gps.location.age() < 2000;
      snap.lat = snap.hasFix ? gps.location.lat() : 0.0;
      snap.lng = snap.hasFix ? gps.location.lng() : 0.0;
      dateStr(snap.date);
      snap.valid = true;
      Serial.println("Signal Received, Servo Opening");
      logGPS("Sample Location");
      while (digitalRead(BUTTON_PIN) == LOW);
    }
  }
  lastBtn = btn;

  switch (mode) {
    case 0: pair1.writeMicroseconds(STOP); break;

    case 1:
      pair1.writeMicroseconds(OPEN);
      if (millis() - prevMillis >= MAG_IGNORE_TIME) {
        if (digitalRead(MAG_PIN) == LOW || millis() - prevMillis >= MAX_MOVE_TIME) {
          pair1.writeMicroseconds(STOP);
          Serial.println(digitalRead(MAG_PIN) == LOW ? "Reed LOW, Rack Open / Servo STOP" : "Servo timeout, Servo STOP");
          float surfaceTemp = NAN, depthTemp = NAN;
          while (ds.selectNext()) {
            uint8_t addr[8]; ds.getAddress(addr); float c = ds.getTempC();
            if      (memcmp(addr, SURFACE_ADDR, 8) == 0) surfaceTemp = c;
            else if (memcmp(addr, DEPTH_ADDR,   8) == 0) depthTemp   = c;
          }
          Serial.print("[TEMP] Surface: "); Serial.print(isnan(surfaceTemp) ? 0.0f : surfaceTemp);
          Serial.print(" C | Depth: ");     Serial.println(isnan(depthTemp)  ? 0.0f : depthTemp);
          if (snap.valid) { writeSDRow(surfaceTemp, depthTemp); snap.valid = false; }
          prevMillis = millis(); mode = 2;
        }
      }
      break;

    case 2:
      pair1.writeMicroseconds(STOP);
      if (millis() - prevMillis >= PAUSE_TIME) { prevMillis = millis(); mode = 3; }
      break;

    case 3:
      pair1.writeMicroseconds(CLOSE);
      if (millis() - prevMillis >= MAG_IGNORE_TIME) {
        if (digitalRead(MAG_PIN) == LOW || millis() - prevMillis >= MAX_MOVE_TIME) {
          pair1.writeMicroseconds(STOP);
          Serial.println(digitalRead(MAG_PIN) == LOW ? "Reed LOW, Rack Closed, Servo STOP" : "Servo Timeout, Servo CLOSE");
          mode = 0;
        }
      }
      break;
  }
}
