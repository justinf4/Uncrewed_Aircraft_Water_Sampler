#include <Arduino.h>
#include "EPD.h"
#include "Pic.h"

extern uint8_t ImageBW[ALLSCREEN_BYTES];

// ── UART from main ESP32 ──────────────────────
// RX = GPIO44 (display receives from main D8)
// TX = GPIO43 (display sends to main D9)
HardwareSerial mainSerial(1);

// ── Display state ─────────────────────────────
char nextA[4]    = "-";     // hardcoded dash until real data arrives
char nextB[4]    = "-";
bool gpsFix      = false;
char csvName[32] = "nodate.csv";
bool hasData     = false;   // true once first DISP message received

// ── Timing ────────────────────────────────────
unsigned long lastPoll = 0;
const unsigned long POLL_INTERVAL = 3000;  // ask main for update every 3s

// ── Draw to EPD ───────────────────────────────
void updateDisplay() {
    EPD_Init();
    EPD_ALL_Fill(WHITE);
    EPD_Update();
    EPD_Clear_R26H();

    // ── Vertical centre divider ───────────────
    EPD_DrawLine(124, 0, 124, 121, BLACK);

    // ══ LEFT PANEL ════════════════════════════

    // Black top banner
    for (uint8_t y = 0; y < 18; y++)
        EPD_DrawLine(0, y, 123, y, BLACK);
    EPD_ShowString(8, 2, "NEXT SAMPLES", WHITE, 16);

    // Syringe 1 ───────────────────────────────
    for (uint8_t y = 25; y < 43; y++)
        EPD_DrawLine(2, y, 6, y, BLACK);
    EPD_DrawRectangle(2, 25, 6, 43, BLACK);
    EPD_DrawLine(7,  25, 70, 25, BLACK);
    EPD_DrawLine(7,  43, 70, 43, BLACK);
    EPD_DrawPoint(7,  26, BLACK);
    EPD_DrawPoint(6,  28, BLACK);
    EPD_DrawPoint(6,  30, BLACK);
    EPD_DrawPoint(6,  34, BLACK);
    EPD_DrawPoint(6,  38, BLACK);
    EPD_DrawPoint(6,  40, BLACK);
    EPD_DrawPoint(7,  42, BLACK);
    EPD_DrawLine(70, 25, 70, 43, BLACK);
    EPD_DrawLine(70, 25, 78, 34, BLACK);
    EPD_DrawLine(70, 43, 78, 34, BLACK);
    EPD_DrawLine(78, 33, 90, 33, BLACK);
    EPD_DrawLine(78, 35, 90, 35, BLACK);
    // number/dash to right
    EPD_ShowString(92, 10, nextA, BLACK, 48);

    // Syringe 2 ───────────────────────────────
    for (uint8_t y = 78; y < 96; y++)
        EPD_DrawLine(2, y, 6, y, BLACK);
    EPD_DrawRectangle(2, 78, 6, 96, BLACK);
    EPD_DrawLine(7,  78, 70, 78, BLACK);
    EPD_DrawLine(7,  96, 70, 96, BLACK);
    EPD_DrawPoint(7,  79, BLACK);
    EPD_DrawPoint(6,  81, BLACK);
    EPD_DrawPoint(6,  83, BLACK);
    EPD_DrawPoint(6,  87, BLACK);
    EPD_DrawPoint(6,  91, BLACK);
    EPD_DrawPoint(6,  93, BLACK);
    EPD_DrawPoint(7,  95, BLACK);
    EPD_DrawLine(70, 78, 70, 96, BLACK);
    EPD_DrawLine(70, 78, 78, 87, BLACK);
    EPD_DrawLine(70, 96, 78, 87, BLACK);
    EPD_DrawLine(78, 86, 90, 86, BLACK);
    EPD_DrawLine(78, 88, 90, 88, BLACK);
    // number/dash to right
    EPD_ShowString(92, 63, nextB, BLACK, 48);

    // ══ RIGHT PANEL ═══════════════════════════

    // Black top banner
    for (uint8_t y = 0; y < 18; y++)
        EPD_DrawLine(125, y, 249, y, BLACK);
    EPD_ShowString(137, 2, "GPS STATE", WHITE, 16);

    // Fix status
    if (gpsFix) {
        EPD_ShowString(126, 26, "FIX ACQUIRED", BLACK, 16);
    } else {
        EPD_ShowString(148, 26, "NO FIX", BLACK, 16);
    }

    // Log file — two lines
    EPD_ShowString(126, 52, "LOG FILE:", BLACK, 16);
    EPD_ShowString(126, 70, csvName,     BLACK, 12);

    EPD_DisplayImage(ImageBW);
    EPD_PartUpdate();
    EPD_Sleep();
}

// ── Parse incoming UART message ───────────────
void parseMessage(const char* msg) {
    if (strncmp(msg, "DISP,", 5) != 0) return;

    char buf[64];
    strncpy(buf, msg + 5, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char tmpA[4], tmpB[4], tmpName[32];
    bool tmpFix;

    char* token = strtok(buf, ",");
    if (!token) return;
    strncpy(tmpA, token, sizeof(tmpA) - 1);
    tmpA[sizeof(tmpA) - 1] = '\0';

    token = strtok(NULL, ",");
    if (!token) return;
    strncpy(tmpB, token, sizeof(tmpB) - 1);
    tmpB[sizeof(tmpB) - 1] = '\0';

    token = strtok(NULL, ",");
    if (!token) return;
    tmpFix = (strcmp(token, "FIXED") == 0);

    token = strtok(NULL, "\n\r");
    if (!token) return;
    strncpy(tmpName, token, sizeof(tmpName) - 1);
    tmpName[sizeof(tmpName) - 1] = '\0';

    bool changed = !hasData ||
                   strcmp(tmpA, nextA) != 0 ||
                   strcmp(tmpB, nextB) != 0 ||
                   tmpFix != gpsFix ||
                   strcmp(tmpName, csvName) != 0;

    strncpy(nextA,   tmpA,   sizeof(nextA)   - 1);
    strncpy(nextB,   tmpB,   sizeof(nextB)   - 1);
    strncpy(csvName, tmpName, sizeof(csvName) - 1);
    gpsFix  = tmpFix;
    hasData = true;

    Serial.print("[SCREEN] Parsed: ");
    Serial.print(nextA); Serial.print(", ");
    Serial.print(nextB); Serial.print(", ");
    Serial.print(gpsFix ? "FIXED" : "NOFIXED"); Serial.print(", ");
    Serial.print(csvName);
    Serial.println(changed ? " → REDRAW" : " → no change");

    if (changed) updateDisplay();
}

// ── Setup ─────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(7, OUTPUT);
    digitalWrite(7, HIGH);

    mainSerial.begin(9600, SERIAL_8N1, 44, 43);  // RX=GPIO44, TX=GPIO43

    // draw dashes immediately on boot
    updateDisplay();

    Serial.println("[SCREEN] Ready");
}

// ── Loop ──────────────────────────────────────
char    inBuf[64];
uint8_t inPos = 0;

void loop() {
    // ── Poll main ESP32 regularly ─────────────
    if (millis() - lastPoll >= POLL_INTERVAL) {
        lastPoll = millis();
        mainSerial.println("POLL");
        Serial.println("[SCREEN] Sent POLL");
    }

    // ── Read incoming UART ────────────────────
    while (mainSerial.available()) {
        char c = mainSerial.read();

        if (c == '\n' || c == '\r') {
            if (inPos > 0) {
                inBuf[inPos] = '\0';
                parseMessage(inBuf);
                inPos = 0;
            }
        } else {
            if (inPos < sizeof(inBuf) - 1) {
                inBuf[inPos++] = c;
            } else {
                inPos = 0;
            }
        }
    }
}