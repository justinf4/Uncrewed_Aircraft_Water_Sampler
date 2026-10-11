// ── main_driver_v5.cpp ────────────────────────────────────────────────────────
//
// Changes from v4:
//   • Display shows fill state — each syringe is FULL or EMPTY independently
//   • sampleCount tracks highest sample number written to CSV
//   • displayA / displayB track what's shown on screen (number + full state)
//   • Reset logic:
//       Both full  → advance both: displayA = sampleCount+1 EMPTY,
//                                  displayB = sampleCount+2 EMPTY
//       Only A full → displayA = displayB (the skipped number) EMPTY,
//                     displayB = sampleCount+2 EMPTY
//                     servoIndex stays so pair 1 (slot B's number) fires next
//   • sendDisplay() now sends:
//       DISP,<numA>,<FULL|EMPTY>,<numB>,<FULL|EMPTY>,<FIXED|NOFIXED>,<fname>
//   • sendDisplay() called whenever: boot, GPS fix, pair reaches state 3,
//     reset completes, GPS fix state changes
//
// ─────────────────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <TinyGPSPlus.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SD.h>
#include <SPI.h>

// ── Pins ──────────────────────────────────────
const int SD_CS_PIN    = D10;
const int RECEIVER_PIN = D6;
const int RESET_PIN    = D5;

const int REED1 = A2;
const int REED2 = A1;
const int REED3 = A0;
const int REED4 = A3;

const int DISP_TX_PIN = D8;
const int DISP_RX_PIN = D9;

const uint8_t CH_S1 = 2, CH_S2 = 1, CH_S3 = 0, CH_S4 = 3;

const int STOP  = 1469;
const int OPEN  = 1000;
const int CLOSE = 2000;

const unsigned long PAUSE_TIME       = 2000;
const unsigned long MAG_IGNORE_TIME  = 2000;
const unsigned long MAX_MOVE_TIME    = 10000;
const unsigned long LED_FLASH_MS     = 150;
const uint32_t      TRIGGER_THRESHOLD = 1800;

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

OneWire oneWire(10);
DallasTemperature sensors(&oneWire);
DeviceAddress SURFACE_ADDR = { 0x28, 0x9C, 0xEB, 0x6F, 0x00, 0x00, 0x00, 0x72 };
DeviceAddress DEPTH_ADDR   = { 0x28, 0x7A, 0x7A, 0x74, 0x00, 0x00, 0x00, 0x3F };

HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
HardwareSerial dispSerial(1);

struct ServoPair { uint8_t chA; int reedA; uint8_t chB; int reedB; };
ServoPair pairs[2] = {
    { CH_S1, REED1, CH_S2, REED2 },
    { CH_S3, REED3, CH_S4, REED4 }
};

struct SnapData { bool valid, hasFix; char date[12]; double lat, lng; };

int           pairMode[2]       = {0, 0};
bool          pairStoppedA[2]   = {false, false};
bool          pairStoppedB[2]   = {false, false};
unsigned long pairPrevMillis[2] = {0, 0};
SnapData      pairSnap[2];

// ── Display state ─────────────────────────────
// displayNumA/B — the sample numbers shown in slot A and B
// displayFullA/B — whether those slots are shown as FULL
// servoIndex     — which pair fires on next trigger (0 or 1)
// sampleCount    — highest sample number successfully written to CSV
int  sampleCount   = 0;
int  displayNumA   = 1;
int  displayNumB   = 2;
bool displayFullA  = false;
bool displayFullB  = false;
int  servoIndex    = 0;   // 0 = pair 0 fills slot A, 1 = pair 1 fills slot B

// ── Which display slot each pair fills ────────
// pairSlot[i] = 0 means pair i fills slot A, 1 fills slot B
int pairSlot[2] = {0, 1};   // pair 0 → slot A, pair 1 → slot B

bool          sentFull[2]        = {false, false};
bool          pairModeEntered[2] = {false, false};  // write servo command once on state entry
unsigned long lastHeartbeat   = 0;
unsigned long ledGreenOffAt   = 0;
unsigned long ledRedOffAt     = 0;
unsigned long ledFlashNext    = 0;
bool          ledRedState     = false;
bool          lastReceiverHigh = false;
bool          lastResetHigh    = false;
bool          fixAnnounced    = false;
bool          lastFixState    = false;   // track GPS state changes
char          logFilename[20] = "/nodate.csv";

// ── Display send ──────────────────────────────
void sendDisplay() {
    const char* fname = (logFilename[0] == '/') ? logFilename + 1 : logFilename;
    dispSerial.printf("DISP,%d,%s,%d,%s,%s,%s\n",
        displayNumA, displayFullA ? "FULL" : "EMPTY",
        displayNumB, displayFullB ? "FULL" : "EMPTY",
        fixAnnounced ? "FIXED" : "NOFIXED",
        fname);
    Serial.printf("[DISP] A=%d/%s B=%d/%s GPS=%s file=%s\n",
        displayNumA, displayFullA?"FULL":"EMPTY",
        displayNumB, displayFullB?"FULL":"EMPTY",
        fixAnnounced?"FIXED":"NOFIXED", fname);
}

// ── Servo helper ──────────────────────────────
void servoWrite(uint8_t ch, int us) { pwm.writeMicroseconds(ch, us); }

// ── GPS helpers ───────────────────────────────
void feedGPS(unsigned long ms) {
    unsigned long s = millis();
    while (millis() - s < ms) while (gpsSerial.available()) gps.encode(gpsSerial.read());
}
static int daysInMonth(int m, int y) {
    const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m==2 && ((y%4==0&&y%100!=0)||y%400==0)) return 29;
    return d[m-1];
}
static int dayOfWeek(int y, int m, int d) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m<3) y--;
    return (y+y/4-y/100+y/400+t[m-1]+d)%7;
}
static int nthWeekday(int n, int dow, int m, int y) {
    int first = dayOfWeek(y, m, 1);
    return 1+((dow-first+7)%7)+(n-1)*7;
}
static int getOffset(int utcYear, int utcMonth, int utcDay, int utcHour) {
    int dstStart = nthWeekday(2,0,3,utcYear);
    int dstEnd   = nthWeekday(1,0,11,utcYear);
    bool afterStart = (utcMonth>3)||(utcMonth==3&&utcDay>dstStart)||
                      (utcMonth==3&&utcDay==dstStart&&utcHour>=7);
    bool beforeEnd  = (utcMonth<11)||(utcMonth==11&&utcDay<dstEnd)||
                      (utcMonth==11&&utcDay==dstEnd&&utcHour<6);
    return (afterStart&&beforeEnd) ? -4 : -5;
}
bool localComponents(int* yr, int* mo, int* dy, int* hr, int* mn, int* sc) {
    if (!gps.date.isValid()||!gps.time.isValid()) return false;
    int utcHour = (int)gps.time.hour();
    int offset  = getOffset((int)gps.date.year(),(int)gps.date.month(),
                             (int)gps.date.day(),utcHour);
    *hr=utcHour+offset; *mn=(int)gps.time.minute();
    *sc=(int)gps.time.second(); *yr=(int)gps.date.year();
    *mo=(int)gps.date.month();  *dy=(int)gps.date.day();
    if (*hr<0) {
        *hr+=24; (*dy)--;
        if (*dy<1) { (*mo)--; if (*mo<1){*mo=12;(*yr)--;} *dy=daysInMonth(*mo,*yr); }
    }
    return true;
}
void dateStr(char* buf) {
    int yr,mo,dy,hr,mn,sc;
    if (localComponents(&yr,&mo,&dy,&hr,&mn,&sc)) sprintf(buf,"%02d/%02d/%04d",mo,dy,yr);
    else if (gps.date.isValid()) sprintf(buf,"%02d/%02d/%04d",gps.date.month(),gps.date.day(),gps.date.year());
    else strcpy(buf,"--/--/----");
}
void estStr(char* buf) {
    int yr,mo,dy,hr,mn,sc;
    if (localComponents(&yr,&mo,&dy,&hr,&mn,&sc)) sprintf(buf,"%02d:%02d:%02d",hr,mn,sc);
    else strcpy(buf,"--:--:--");
}
void logGPS(const char* label) {
    feedGPS(100);
    char d[12],t[10]; dateStr(d); estStr(t);
    Serial.printf("[GPS] %s | %s %s EST | ",label,d,t);
    if (gps.location.isValid())
        Serial.printf("LAT=%.6f LNG=%.6f ALT=%.1f\n",
            gps.location.lat(),gps.location.lng(),gps.altitude.meters());
    else Serial.println("NO FIX");
}

// ── SD helpers ────────────────────────────────
void initLogFile(const char* filename) {
    strcpy(logFilename, filename);
    if (!SD.exists(logFilename)) {
        File f = SD.open(logFilename, FILE_WRITE);
        if (f) { f.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C"); f.close(); }
        sampleCount=0;
    } else {
        File f = SD.open(logFilename, FILE_READ);
        int lines=0;
        if (f) { while (f.available()) { if (f.read()=='\n') lines++; } f.close(); }
        sampleCount = max(0, lines-1);
    }
    // Restore display state from sampleCount
    // Both slots start empty, pointing at next two samples
    displayNumA  = sampleCount + 1;
    displayNumB  = sampleCount + 2;
    displayFullA = false;
    displayFullB = false;
    // servoIndex: pair 0 fills slot A first
    servoIndex = 0;
    pairSlot[0] = 0;   // pair 0 → slot A
    pairSlot[1] = 1;   // pair 1 → slot B
    Serial.printf("[SD] %s  samples=%d  display A=%d B=%d\n",
        logFilename, sampleCount, displayNumA, displayNumB);
    sendDisplay();
}

void writeSDRow(float surfaceTemp, float depthTemp, int pairIdx) {
    SnapData& snap = pairSnap[pairIdx];
    if (!SD.exists(logFilename)) {
        File hdr = SD.open(logFilename, FILE_WRITE);
        if (hdr) { hdr.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C"); hdr.close(); }
        else { Serial.println("[SD] ERROR opening for header"); return; }
    }
    File f = SD.open(logFilename, FILE_APPEND);
    if (!f) { Serial.println("[SD] ERROR opening for append"); return; }
    float st = isnan(surfaceTemp)?0.0f:surfaceTemp;
    float dt = isnan(depthTemp)  ?0.0f:depthTemp;
    char ts[10]; estStr(ts);
    // Use the display slot number for this pair as the CSV count
    int csvCount = (pairSlot[pairIdx]==0) ? displayNumA : displayNumB;
    f.print(csvCount); f.print(",");
    f.print(snap.date); f.print(",");
    f.print(ts); f.print(",");
    if (snap.hasFix) { f.print(snap.lat,6); f.print(","); f.print(snap.lng,6); }
    else             { f.print("NO FIX,NO FIX"); }
    f.print(","); f.print(st,2); f.print(","); f.println(dt,2);
    f.close();
    // Update sampleCount to the highest number written
    if (csvCount > sampleCount) sampleCount = csvCount;
    Serial.printf("[SD] Wrote row count=%d (pair %d slot %s)\n",
        csvCount, pairIdx+1, pairSlot[pairIdx]==0?"A":"B");
}

// ── File manager UART handlers ─────────────────
void handleFLIST() {
    if (!SD.exists(logFilename)) { dispSerial.println("FDONE,0"); return; }
    File f = SD.open(logFilename, FILE_READ);
    if (!f)                      { dispSerial.println("FDONE,0"); return; }
    int rowCount=0; bool firstLine=true;
    char lineBuf[100]; uint8_t lpos=0;
    while (f.available()) {
        char c=f.read();
        if (c=='\n'||c=='\r') {
            if (lpos>0) {
                lineBuf[lpos]='\0';
                if (firstLine) { firstLine=false; }
                else { dispSerial.print("FROW,"); dispSerial.println(lineBuf); rowCount++; }
                lpos=0;
            }
        } else { if (lpos<99) lineBuf[lpos++]=c; }
    }
    f.close();
    dispSerial.printf("FDONE,%d\n",rowCount);
    Serial.printf("[FM] Sent %d rows\n",rowCount);
}

void handleFDEL(int targetCount) {
    if (!SD.exists(logFilename)) { dispSerial.println("FDEL_ERR"); return; }
    File f=SD.open(logFilename,FILE_READ);
    if (!f) { dispSerial.println("FDEL_ERR"); return; }
    const int MAXLINES=210;
    static char lines[MAXLINES][100];
    int lineCount=0; char lineBuf[100]; uint8_t lpos=0;
    while (f.available()&&lineCount<MAXLINES) {
        char c=f.read();
        if (c=='\n'||c=='\r') {
            if (lpos>0) { lineBuf[lpos]='\0'; strncpy(lines[lineCount++],lineBuf,99); lpos=0; }
        } else { if (lpos<99) lineBuf[lpos++]=c; }
    }
    f.close();
    SD.remove(logFilename);
    File out=SD.open(logFilename,FILE_WRITE);
    if (!out) { dispSerial.println("FDEL_ERR"); return; }
    bool deleted=false;
    for (int i=0;i<lineCount;i++) {
        if (i==0) { out.println(lines[i]); }
        else if (atoi(lines[i])==targetCount) { deleted=true; }
        else { out.println(lines[i]); }
    }
    out.close();
    dispSerial.println(deleted?"FDEL_OK":"FDEL_ERR");
    Serial.printf("[FM] %s row %d\n",deleted?"Deleted":"Not found",targetCount);
}

void handleFWIPE() {
    SD.remove(logFilename);
    File f=SD.open(logFilename,FILE_WRITE);
    if (!f) { dispSerial.println("FWIPE_ERR"); return; }
    f.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C");
    f.close();
    // Full reset of display state
    sampleCount  = 0;
    displayNumA  = 1; displayFullA = false;
    displayNumB  = 2; displayFullB = false;
    servoIndex   = 0;
    pairSlot[0]  = 0; pairSlot[1] = 1;
    sendDisplay();
    dispSerial.println("FWIPE_OK");
    Serial.printf("[FM] Wiped %s\n",logFilename);
}

void ledSet(int pin, bool on) { digitalWrite(pin, on?LOW:HIGH); }

// ── Setup ─────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);
    dispSerial.begin(9600, SERIAL_8N1, DISP_RX_PIN, DISP_TX_PIN);
    Wire.begin();
    pwm.begin();
    pwm.setOscillatorFrequency(29000000);
    pwm.setPWMFreq(50);
    delay(10);
    pwm.writeMicroseconds(CH_S1,STOP); pwm.writeMicroseconds(CH_S2,STOP);
    pwm.writeMicroseconds(CH_S3,STOP); pwm.writeMicroseconds(CH_S4,STOP);
    pinMode(RECEIVER_PIN,INPUT); pinMode(RESET_PIN,INPUT);
    pinMode(REED1,INPUT_PULLUP); pinMode(REED2,INPUT_PULLUP);
    pinMode(REED3,INPUT_PULLUP); pinMode(REED4,INPUT_PULLUP);
    pinMode(LEDG,OUTPUT); ledSet(LEDG,false);
    pinMode(LEDR,OUTPUT); ledSet(LEDR,false);
    pinMode(LEDB,OUTPUT); ledSet(LEDB,false);
    SPI.begin(D13,D12,D11,D10);
    if (SD.begin(SD_CS_PIN)) { Serial.println("[SD] Ready"); initLogFile("/nodate.csv"); }
    else                     { Serial.println("[SD] FAILED"); sendDisplay(); }
    delay(100);
    gpsSerial.begin(9600,SERIAL_8N1,D3,D4);
    sensors.begin();
    sensors.setResolution(SURFACE_ADDR,12);
    sensors.setResolution(DEPTH_ADDR,12);
    sensors.requestTemperatures();
    delay(800);
    lastFixState = fixAnnounced;
    Serial.println("[SYS] driver_V5 Ready");
}

// ── UART receive ──────────────────────────────
char    dispInBuf[32];
uint8_t dispInPos=0;

void processDispMessage(const char* msg) {
    if (strcmp(msg,"POLL")==0)             { sendDisplay(); return; }
    if (strcmp(msg,"FLIST")==0)            { handleFLIST(); return; }
    if (strncmp(msg,"FDEL,",5)==0)         { handleFDEL(atoi(msg+5)); return; }
    if (strcmp(msg,"FWIPE")==0)            { handleFWIPE(); return; }
}

// ── Loop ──────────────────────────────────────
void loop() {
    // ── Receive from display ──────────────────
    while (dispSerial.available()) {
        char c=dispSerial.read();
        if (c=='\n'||c=='\r') {
            if (dispInPos>0) {
                dispInBuf[dispInPos]='\0';
                processDispMessage(dispInBuf);
                dispInPos=0;
            }
        } else { if (dispInPos<sizeof(dispInBuf)-1) dispInBuf[dispInPos++]=c; else dispInPos=0; }
    }

    // ── GPS feed ──────────────────────────────
    while (gpsSerial.available()) gps.encode(gpsSerial.read());

    // GPS fix acquired → rename file, send display update
    if (!fixAnnounced && gps.location.isValid() && gps.date.isValid()) {
        fixAnnounced=true;
        int yr,mo,dy,hr,mn,sc; char fname[20];
        if (localComponents(&yr,&mo,&dy,&hr,&mn,&sc))
            sprintf(fname,"/%04d%02d%02d.csv",yr,mo,dy);
        else
            sprintf(fname,"/%04d%02d%02d.csv",gps.date.year(),gps.date.month(),gps.date.day());
        if (strcmp(logFilename,fname)!=0) initLogFile(fname);
        else sendDisplay();
        lastFixState=true;
        Serial.println("[GPS] Fix acquired");
    }

    // GPS state change (fix lost) → send display update
    if (fixAnnounced != lastFixState) {
        lastFixState = fixAnnounced;
        sendDisplay();
    }

    // ── Derived flags ─────────────────────────
    bool anyActive  = (pairMode[0]>0 || pairMode[1]>0);
    bool nextIdle   = (pairMode[servoIndex]==0);
    bool anyWaiting = (pairMode[0]==3 || pairMode[1]==3);

    // ── CH1 trigger ───────────────────────────
    bool triggered=false;
    if (nextIdle) {
        uint32_t pulse   = pulseIn(RECEIVER_PIN,HIGH,25000);
        bool recvHigh    = (pulse>TRIGGER_THRESHOLD);
        triggered        = (recvHigh && !lastReceiverHigh);
        lastReceiverHigh = recvHigh;
    } else { lastReceiverHigh=false; }

    // ── CH2 reset ─────────────────────────────
    bool resetTriggered=false;
    if (anyWaiting) {
        uint32_t resetPulse = pulseIn(RESET_PIN,HIGH,25000);
        bool resetHigh      = (resetPulse>TRIGGER_THRESHOLD);
        resetTriggered      = (resetHigh && !lastResetHigh);
        lastResetHigh       = resetHigh;
    } else { lastResetHigh=false; }

    // ── Heartbeat ─────────────────────────────
    if (millis()-lastHeartbeat>=5000) {
        lastHeartbeat=millis();
        char t[10]; estStr(t);
        Serial.printf("[STATUS] p0=%d p1=%d | sIdx=%d | samples=%d A=%d/%s B=%d/%s | gps=%s | %s\n",
            pairMode[0],pairMode[1],servoIndex,sampleCount,
            displayNumA,displayFullA?"F":"E",
            displayNumB,displayFullB?"F":"E",
            fixAnnounced?"FIX":"SEARCH",t);
    }

    // ── Trigger: open next idle pair ──────────
    if (triggered && nextIdle) {
        int idx = servoIndex;
        pairStoppedA[idx]=false; pairStoppedB[idx]=false;
        pairPrevMillis[idx]=millis();
        pairModeEntered[idx]=false;
        pairMode[idx]=1;

        feedGPS(100);
        pairSnap[idx].hasFix = gps.location.isValid();
        pairSnap[idx].lat    = pairSnap[idx].hasFix ? gps.location.lat() : 0.0;
        pairSnap[idx].lng    = pairSnap[idx].hasFix ? gps.location.lng() : 0.0;
        pairSnap[idx].valid  = true;
        dateStr(pairSnap[idx].date);

        // Advance servoIndex to other pair for next trigger
        servoIndex = 1 - servoIndex;

        ledSet(LEDG,true); ledGreenOffAt=millis()+1000;
        if (!anyActive) ledFlashNext=millis();
        logGPS("Sample");
        Serial.printf("[TRIG] Pair %d (slot %s) opening — sample #%d\n",
            idx+1, pairSlot[idx]==0?"A":"B",
            pairSlot[idx]==0 ? displayNumA : displayNumB);
    }

    // ── Reset: close all waiting pairs ────────
    // ── Reset: close all waiting pairs ────────
    if (resetTriggered) {
        bool aFull = displayFullA;
        bool bFull = displayFullB;

        for (int i=0;i<2;i++) {
            if (pairMode[i]==3) {
                pairStoppedA[i]=false; pairStoppedB[i]=false;
                pairPrevMillis[i]=millis();
                pairModeEntered[i]=false;
                pairMode[i]=4;
                Serial.printf("[RESET] Closing pair %d\n",i+1);
            }
        }

        // ── Advance display state ──────────────
        if (aFull && bFull) {
            // Both taken — straight advance to next pair
            displayNumA  = sampleCount + 1;
            displayFullA = false;
            displayNumB  = sampleCount + 2;
            displayFullB = false;
            // Reset slot assignments
            pairSlot[0]=0; pairSlot[1]=1;
            servoIndex=0;
            Serial.printf("[RESET] Both full → next pair A=%d B=%d\n",
                displayNumA,displayNumB);
        }
        else if (aFull && !bFull) {
            // Only A taken — B number gets promoted to slot A
            int skippedNum = displayNumB;
            displayNumA  = skippedNum;
            displayFullA = false;
            displayNumB  = sampleCount + 2;  // +2 because skippedNum already takes +1
            displayFullB = false;
            // Next trigger goes to pair that fills slot A (whichever that is)
            // Find which pair was slot B and reassign it to slot A
            for (int i=0;i<2;i++) {
                if (pairSlot[i]==1) pairSlot[i]=0;   // promote B→A
                else                pairSlot[i]=1;   // other pair → B
            }
            // Point servoIndex at whichever pair now fills slot A
            servoIndex = (pairSlot[0]==0) ? 0 : 1;
            Serial.printf("[RESET] Only A full → promote B#%d to A, new B=%d\n",
                skippedNum, displayNumB);
        }
        else if (!aFull && bFull) {
            // Only B taken (unusual) — A number gets promoted to slot B?
            // Treat symmetrically: skipped A becomes new slot A
            int skippedNum = displayNumA;
            displayNumA  = skippedNum;
            displayFullA = false;
            displayNumB  = sampleCount + 2;  // +2 because skippedNum already takes +1
            displayFullB = false;
            pairSlot[0]=0; pairSlot[1]=1;
            servoIndex=0;
            Serial.printf("[RESET] Only B full → keep A#%d empty, new B=%d\n",
                skippedNum, displayNumB);
        }
        else {
            // Neither taken — keep same numbers, just re-arm
            displayFullA=false; displayFullB=false;
            servoIndex=0;
            Serial.println("[RESET] Neither taken — re-arming same pair");
        }

        sendDisplay();
    }

    // ── LED ───────────────────────────────────
    if (ledGreenOffAt && millis()>=ledGreenOffAt) { ledSet(LEDG,false); ledGreenOffAt=0; }
    anyActive=(pairMode[0]>0||pairMode[1]>0);
    if (anyActive && ledRedOffAt==0 && millis()>=ledFlashNext) {
        ledRedState=!ledRedState; ledSet(LEDR,ledRedState);
        ledFlashNext=millis()+LED_FLASH_MS;
    }
    if (ledRedOffAt && millis()>=ledRedOffAt) {
        ledSet(LEDR,false); ledRedOffAt=0; ledRedState=false;
    }

    // ── Per-pair state machines ────────────────
    // Servo commands are written ONCE on state entry — PCA9685 holds the value.
    // No continuous writes: avoids STOP→OPEN stutter every loop iteration.
    for (int i=0;i<2;i++) {
        ServoPair& p=pairs[i];
        unsigned long elapsed=millis()-pairPrevMillis[i];

        switch (pairMode[i]) {
            case 0: // Idle — continuously hold servos at stop
                servoWrite(p.chA,STOP);
                servoWrite(p.chB,STOP);
                break;

            case 1: // Opening — write OPEN once on entry
                if (!pairModeEntered[i]) {
                    pairModeEntered[i]=true;
                    servoWrite(p.chA,OPEN);
                    servoWrite(p.chB,OPEN);
                }
                if (elapsed>=MAG_IGNORE_TIME) {
                    if (!pairStoppedA[i]&&(digitalRead(p.reedA)==LOW||elapsed>=MAX_MOVE_TIME)) {
                        pairStoppedA[i]=true;
                        servoWrite(p.chA,STOP);
                        Serial.printf("[SERVO] Pair %d A open — stopped\n",i+1);
                    }
                    if (!pairStoppedB[i]&&(digitalRead(p.reedB)==LOW||elapsed>=MAX_MOVE_TIME)) {
                        pairStoppedB[i]=true;
                        servoWrite(p.chB,STOP);
                        Serial.printf("[SERVO] Pair %d B open — stopped\n",i+1);
                    }
                    if (pairStoppedA[i]&&pairStoppedB[i]) {
                        sensors.requestTemperatures();
                        float st=sensors.getTempC(SURFACE_ADDR);
                        float dt=sensors.getTempC(DEPTH_ADDR);
                        if (fabsf(st-85.0f)<0.5f||st==DEVICE_DISCONNECTED_C) st=NAN;
                        if (fabsf(dt-85.0f)<0.5f||dt==DEVICE_DISCONNECTED_C) dt=NAN;
                        Serial.printf("[TEMP] Pair %d S=%.2f D=%.2f\n",i+1,
                            isnan(st)?0.0f:st,isnan(dt)?0.0f:dt);
                        if (pairSnap[i].valid) { writeSDRow(st,dt,i); pairSnap[i].valid=false; }
                        pairPrevMillis[i]=millis(); pairMode[i]=2;
                    }
                }
                break;

            case 2: // Pause — servos already at STOP, hold
                if (elapsed>=PAUSE_TIME) { pairPrevMillis[i]=millis(); pairMode[i]=3; }
                break;

            case 3: // Waiting for reset — update display fill state once on entry
                 servoWrite(p.chA,STOP);
                 servoWrite(p.chB,STOP);

                if (!sentFull[i]) {
                    sentFull[i]=true;
                    if (pairSlot[i]==0) displayFullA=true;
                    else                displayFullB=true;
                    sendDisplay();
                    Serial.printf("[STATE3] Pair %d slot %s now FULL — waiting for reset\n",
                        i+1, pairSlot[i]==0?"A":"B");
                }
                break;

            case 4: // Closing — write CLOSE once on entry
                if (!pairModeEntered[i]) {
                    pairModeEntered[i]=true;
                    servoWrite(p.chA,CLOSE);
                    servoWrite(p.chB,CLOSE);
                }
                if (elapsed>=MAG_IGNORE_TIME) {
                    if (!pairStoppedA[i]&&(digitalRead(p.reedA)==LOW||elapsed>=MAX_MOVE_TIME)) {
                        pairStoppedA[i]=true;
                        servoWrite(p.chA,STOP);
                        Serial.printf("[SERVO] Pair %d A closed — stopped\n",i+1);
                    }
                    if (!pairStoppedB[i]&&(digitalRead(p.reedB)==LOW||elapsed>=MAX_MOVE_TIME)) {
                        pairStoppedB[i]=true;
                        servoWrite(p.chB,STOP);
                        Serial.printf("[SERVO] Pair %d B closed — stopped\n",i+1);
                    }
                    if (pairStoppedA[i]&&pairStoppedB[i]) {
                        pairMode[i]=0;
                        ledSet(LEDR,true); ledRedState=true;
                        ledRedOffAt=millis()+1000; ledFlashNext=0;
                        Serial.printf("[SERVO] Pair %d cycle complete\n",i+1);
                    }
                }
                break;
        }
    }

    // Clear sentFull when pair returns to idle so next cycle updates display
    for (int i=0;i<2;i++)
        if (pairMode[i]==0) sentFull[i]=false;
}
