// ── screen_driver_v6.cpp ─────────────────────────────────────────────────────
#include <Arduino.h>
#include "EPD.h"
#include "Pic.h"
#include "MapPinIcons.h"

extern uint8_t ImageBW[ALLSCREEN_BYTES];

#define PIN_MENU        2
#define PIN_EXIT        1
#define PIN_ROT_DOWN    4
#define PIN_ROT_UP      6
#define PIN_ROT_CLICK   5

HardwareSerial mainSerial(1);

enum ScreenMode { MODE_MAIN, MODE_FILEMANAGER, MODE_CONFIRM_DEL, MODE_CONFIRM_WIPE };
ScreenMode screenMode = MODE_MAIN;

// ── Main screen state ─────────────────────────────────────────────────────────
int  numA    = 1;  bool fullA  = false;
int  numB    = 2;  bool fullB  = false;
bool gpsFix  = false;
char csvName[32] = "nodate.csv";
bool hasData = false;

unsigned long lastPoll = 0;
const unsigned long POLL_INTERVAL = 3000;

// ── File manager state ────────────────────────────────────────────────────────
#define FM_MAX_ROWS 200
#define FM_VISIBLE  3      // 3 data rows visible (leaves room for header + col labels + footer)

struct CsvRow {
    int  count;
    char date[12];   // MM/DD/YYYY
    char time[10];   // HH:MM:SS
    char fix[8];     // FIX / NOFX
    char surfT[8];
    char depT[8];
};

CsvRow fmRows[FM_MAX_ROWS];
int    fmRowCount     = 0;
int    fmScroll       = 0;
int    fmSelected     = 0;
bool   fmLoading      = false;
bool   fmWaitingReply = false;

// ── Button debounce ───────────────────────────────────────────────────────────
struct Button { uint8_t pin; bool lastState; unsigned long lastEdge; };
Button btnMenu     = { PIN_MENU,      HIGH, 0 };
Button btnExit     = { PIN_EXIT,      HIGH, 0 };
Button btnRotDown  = { PIN_ROT_DOWN,  HIGH, 0 };
Button btnRotUp    = { PIN_ROT_UP,    HIGH, 0 };
Button btnRotClick = { PIN_ROT_CLICK, HIGH, 0 };
const unsigned long DEBOUNCE_MS = 50;

bool edgeDetect(Button& b) {
    bool now = digitalRead(b.pin);
    unsigned long t = millis();
    if (now != b.lastState && (t - b.lastEdge) > DEBOUNCE_MS) {
        b.lastEdge = t; b.lastState = now;
        if (now == LOW) return true;
    }
    return false;
}

// ── Map pin bitmap ────────────────────────────────────────────────────────────
void drawMapPin(int px, int py, uint8_t color) {
    for (int row = 0; row < 40; row++)
        for (int col = 0; col < 40; col++) {
            uint8_t b = pgm_read_byte(&mapPinIcon_40[row*5 + col/8]);
            if (b & (0x80 >> (col%8)))
                EPD_DrawPoint(px+col, py+row, color);
        }
}

// ── No-fix overlay ────────────────────────────────────────────────────────────
void drawNoFix(int cx, int cy, int r) {
    EPD_DrawCircle(cx, cy, r,     BLACK, 0);
    EPD_DrawCircle(cx, cy, r - 1, BLACK, 0);
    int x1=cx+(int)(r*-0.707f), y1=cy+(int)(r*-0.707f);
    int x2=cx+(int)(r* 0.707f), y2=cy+(int)(r* 0.707f);
    EPD_DrawLine(x1,   y1,   x2,   y2,   BLACK);
    EPD_DrawLine(x1+1, y1,   x2+1, y2,   BLACK);
    EPD_DrawLine(x1,   y1+1, x2,   y2+1, BLACK);
}

// ── Syringe ───────────────────────────────────────────────────────────────────
void drawSyringe(int x, int y, int w, int h, bool full) {
    int plW = max(2, w/16);
    for (int fy=y+h/6; fy<=y+h-h/6; fy++)
        EPD_DrawLine(x, fy, x+plW, fy, BLACK);
    int rodX=x+plW, rodH=max(2,h/5), rodY=y+h/2-rodH/2, rodW=w*20/100;
    for (int fy=rodY; fy<=rodY+rodH; fy++)
        EPD_DrawLine(rodX, fy, rodX+rodW, fy, BLACK);
    int stX=rodX+rodW, stW=max(3,w/13);
    for (int fy=y+h/8; fy<=y+h-h/8; fy++)
        EPD_DrawLine(stX, fy, stX+stW, fy, BLACK);
    int barX=stX+stW, barW=w*56/100;
    if (full) {
        for (int fy=y; fy<=y+h; fy++) EPD_DrawLine(barX, fy, barX+barW, fy, BLACK);
    } else {
        EPD_DrawRectangle(barX, y, barX+barW, y+h, BLACK);
    }
    int tipBX=barX+barW, tipTipX=tipBX+w*13/100;
    int midY=y+h/2, tipHalf=max(2,h/6);
    for (int fy=midY-tipHalf; fy<=midY+tipHalf; fy++) {
        int reach=(int)((float)(tipHalf-abs(fy-midY))/tipHalf*(tipTipX-tipBX));
        EPD_DrawLine(tipBX, fy, tipBX+reach, fy, BLACK);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN SCREEN
// ═════════════════════════════════════════════════════════════════════════════
void drawMainScreen() {
    EPD_Init();
    EPD_ALL_Fill(WHITE);
    EPD_Update();
    EPD_Clear_R26H();

    // FILE MGR banner — top right
    for (uint8_t fy = 0; fy <= 13; fy++)
        EPD_DrawLine(185, fy, 249, fy, BLACK);
    EPD_ShowString(188, 2, "FILE MGR", WHITE, 12);

    // Syringe A — y 8..51
    drawSyringe(2, 8, 120, 44, fullA);
    {
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d - %s", numA, fullA ? "FULL" : "EMPTY");
        int strW  = strlen(lbl) * 7;
        int labelX = max(44, 76 - strW/2);
        int labelY = 8 + 44/2 - 6;
        EPD_ShowString(labelX, labelY, lbl, fullA ? WHITE : BLACK, 12);
    }

    // Syringe B — y 66..109
    drawSyringe(2, 66, 120, 44, fullB);
    {
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d - %s", numB, fullB ? "FULL" : "EMPTY");
        int strW  = strlen(lbl) * 7;
        int labelX = max(44, 76 - strW/2);
        int labelY = 66 + 44/2 - 6;
        EPD_ShowString(labelX, labelY, lbl, fullB ? WHITE : BLACK, 12);
    }

    // GPS icon + status + log
    drawMapPin(142, 25, BLACK);
    if (!gpsFix) drawNoFix(162, 45, 20);
    const char* gpsText = gpsFix ? "GPS FIX" : "GPS NO FIX";
    EPD_ShowString(186, 37, gpsText, BLACK, 12);
    const char* dn = (csvName[0]=='/') ? csvName+1 : csvName;
    char logLine[40];
    snprintf(logLine, sizeof(logLine), "LOG: %s", dn);
    EPD_ShowString(142, 67, logLine, BLACK, 12);

    EPD_DisplayImage(ImageBW);
    EPD_Update();   // full LUT — correctly clears black→white on EMPTY syringes
    EPD_Sleep();
}

// ═════════════════════════════════════════════════════════════════════════════
// FILE MANAGER — LOADING SCREEN
// ═════════════════════════════════════════════════════════════════════════════
void drawFM_Loading() {
    EPD_Init();
    EPD_ALL_Fill(WHITE);
    EPD_Update();
    EPD_Clear_R26H();

    // Header bar
    for (uint8_t fy=0; fy<14; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2, 2, "FILE MANAGER", WHITE, 12);

    EPD_ShowString(60, 50, "LOADING CSV...", BLACK, 14);

    // Footer
    for (uint8_t fy=110; fy<=121; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(180, 112, "EXIT=BACK", WHITE, 12);

    EPD_DisplayImage(ImageBW);
    EPD_PartUpdate();
    EPD_Sleep();
}

// ═════════════════════════════════════════════════════════════════════════════
// FILE MANAGER — MAIN TABLE VIEW
//
// Layout (250×122):
//
//  y=0..13   ┌─ FILE MANAGER: filename (N rows) ──────────────────────────┐  BLACK bar
//  y=14..14  └────────────────────────────────────────────────────────────┘
//  y=15..26  │ ROW │ DATE       │ TIME     │ GPS  │ SURF │ DEPT │          │  col headers (inverted)
//  y=27..27  ├─────┼────────────┼──────────┼──────┼──────┼──────┤
//  y=28..55  │  1  │ MM/DD/YYYY │ HH:MM:SS │ FIX  │22.10 │18.30 │  ← row 0 (selected = inverted)
//  y=56..56  ├─────┴────────────┴──────────┴──────┴──────┴──────┤
//  y=57..84  │  2  │ ...                                         │
//  y=85..85  ├─────┴─────────────────────────────────────────────┤
//  y=86..108 │  3  │ ...                                         │
//  y=109..109├─────────────────────────────────────────────────  ┤
//  y=110..121└─ CLK=DEL  HOLD=WIPE  ↑↓=SCROLL  EXIT=BACK ───────┘  BLACK bar
//
// ═════════════════════════════════════════════════════════════════════════════
void drawFM_Table() {
    EPD_Init();
    EPD_ALL_Fill(WHITE);
    EPD_Update();
    EPD_Clear_R26H();

    // ── Header bar ────────────────────────────────────────────────────────────
    for (uint8_t fy=0; fy<14; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2, 2, "FILE MANAGER:", WHITE, 12);
    const char* dn = (csvName[0]=='/') ? csvName+1 : csvName;
    EPD_ShowString(100, 2, dn, WHITE, 12);
    // Row count right-aligned area
    char cntStr[10]; snprintf(cntStr, sizeof(cntStr), "%dR", fmRowCount);
    EPD_ShowString(222, 2, cntStr, WHITE, 12);

    // ── Column header row (y=14..26, black bg) ────────────────────────────────
    for (uint8_t fy=14; fy<=26; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    // Col positions:  ROW=2  DATE=22  TIME=90  GPS=152  SURF=178  DEPT=216
    EPD_ShowString(2,   15, "#",     WHITE, 12);
    EPD_ShowString(16,  15, "DATE",  WHITE, 12);
    EPD_ShowString(84,  15, "TIME",  WHITE, 12);
    EPD_ShowString(148, 15, "GPS",   WHITE, 12);
    EPD_ShowString(172, 15, "SURF",  WHITE, 12);
    EPD_ShowString(210, 15, "DEPT",  WHITE, 12);

    // ── Separator below col headers ───────────────────────────────────────────
    EPD_DrawLine(0, 27, 249, 27, BLACK);

    // ── Empty state ───────────────────────────────────────────────────────────
    if (fmRowCount == 0) {
        EPD_ShowString(30, 60, "NO SAMPLES IN FILE", BLACK, 14);
        for (uint8_t fy=110; fy<=121; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
        EPD_ShowString(2,   112, "HOLD=WIPE", WHITE, 12);
        EPD_ShowString(180, 112, "EXIT=BACK", WHITE, 12);
        EPD_DisplayImage(ImageBW); EPD_PartUpdate(); EPD_Sleep();
        return;
    }

    // ── Data rows — 3 visible, each 27px tall ─────────────────────────────────
    // Row heights: 28..54, 55..81, 82..108  (27px each, separator at bottom)
    for (int i = 0; i < FM_VISIBLE; i++) {
        int idx  = fmScroll + i;
        if (idx >= fmRowCount) break;

        int  rowY  = 28 + i * 27;
        bool isSel = (idx == fmSelected);
        uint8_t fg = isSel ? WHITE : BLACK;

        // Invert selected row
        if (isSel)
            for (uint8_t fy=rowY; fy<rowY+27; fy++)
                EPD_DrawLine(0, fy, 249, fy, BLACK);

        CsvRow& r = fmRows[idx];

        // # (count)
        char cnt[6]; snprintf(cnt, sizeof(cnt), "%d", r.count);
        EPD_ShowString(2, rowY+2, cnt, fg, 12);

        // Date (MM/DD/YYYY → show MM/DD/YY to save space)
        char dateShort[10];
        strncpy(dateShort, r.date, 8); dateShort[8]='\0'; // MM/DD/YY
        EPD_ShowString(16, rowY+2, dateShort, fg, 12);

        // Time (HH:MM:SS → show HH:MM)
        char timeShort[6];
        strncpy(timeShort, r.time, 5); timeShort[5]='\0';
        EPD_ShowString(84, rowY+2, timeShort, fg, 12);

        // GPS fix
        EPD_ShowString(148, rowY+2, r.fix, fg, 12);

        // Surf temp
        EPD_ShowString(172, rowY+2, r.surfT, fg, 12);

        // Depth temp
        EPD_ShowString(210, rowY+2, r.depT, fg, 12);

        // Row separator (skip on last visible or if selected)
        if (!isSel && i < FM_VISIBLE-1)
            EPD_DrawLine(0, rowY+26, 249, rowY+26, BLACK);
    }

    // ── Scroll indicators ─────────────────────────────────────────────────────
    if (fmScroll > 0)
        EPD_ShowString(241, 28, "^", BLACK, 12);
    if (fmScroll + FM_VISIBLE < fmRowCount)
        EPD_ShowString(241, 96, "v", BLACK, 12);

    // ── Footer bar ────────────────────────────────────────────────────────────
    for (uint8_t fy=110; fy<=121; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2,   112, "CLK=DEL  HOLD=WIPE", WHITE, 12);
    EPD_ShowString(180, 112, "EXIT=BACK", WHITE, 12);

    EPD_DisplayImage(ImageBW);
    EPD_PartUpdate();
    EPD_Sleep();
}

// ═════════════════════════════════════════════════════════════════════════════
// FILE MANAGER — CONFIRM DELETE ROW
// ═════════════════════════════════════════════════════════════════════════════
void drawFM_ConfirmDelete() {
    EPD_Init();
    EPD_ALL_Fill(WHITE);
    EPD_Update();
    EPD_Clear_R26H();

    // Header
    for (uint8_t fy=0; fy<14; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2, 2, "DELETE SAMPLE", WHITE, 12);

    // Box border
    EPD_DrawRectangle(10, 20, 239, 100, BLACK);
    EPD_DrawRectangle(11, 21, 238, 99,  BLACK);  // double border

    // Content
    if (fmSelected < fmRowCount) {
        CsvRow& r = fmRows[fmSelected];
        char line1[32], line2[32], line3[32];
        snprintf(line1, sizeof(line1), "SAMPLE # %d", r.count);
        snprintf(line2, sizeof(line2), "DATE:  %s", r.date);
        snprintf(line3, sizeof(line3), "TIME:  %s", r.time);
        EPD_ShowString(20, 28, line1, BLACK, 14);
        EPD_DrawLine(12, 45, 237, 45, BLACK);
        EPD_ShowString(20, 48, line2, BLACK, 12);
        EPD_ShowString(20, 62, line3, BLACK, 12);
        EPD_DrawLine(12, 76, 237, 76, BLACK);
        EPD_ShowString(20, 79, "Permanently delete this row?", BLACK, 12);
    }

    // Footer
    for (uint8_t fy=110; fy<=121; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2,   112, "CLK=CONFIRM", WHITE, 12);
    EPD_ShowString(180, 112, "EXIT=CANCEL", WHITE, 12);

    EPD_DisplayImage(ImageBW);
    EPD_PartUpdate();
    EPD_Sleep();
}

// ═════════════════════════════════════════════════════════════════════════════
// FILE MANAGER — CONFIRM WIPE
// ═════════════════════════════════════════════════════════════════════════════
void drawFM_ConfirmWipe() {
    EPD_Init();
    EPD_ALL_Fill(WHITE);
    EPD_Update();
    EPD_Clear_R26H();

    // Header
    for (uint8_t fy=0; fy<14; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2, 2, "WIPE ALL DATA", WHITE, 12);

    EPD_DrawRectangle(10, 20, 239, 100, BLACK);
    EPD_DrawRectangle(11, 21, 238, 99,  BLACK);

    const char* dn = (csvName[0]=='/') ? csvName+1 : csvName;
    char line1[40];
    snprintf(line1, sizeof(line1), "File: %s", dn);
    EPD_ShowString(20, 28, line1, BLACK, 12);
    char line2[20];
    snprintf(line2, sizeof(line2), "%d samples total", fmRowCount);
    EPD_ShowString(20, 42, line2, BLACK, 12);
    EPD_DrawLine(12, 56, 237, 56, BLACK);
    EPD_ShowString(20, 60, "ALL DATA WILL BE", BLACK, 14);
    EPD_ShowString(20, 76, "PERMANENTLY DELETED", BLACK, 14);

    for (uint8_t fy=110; fy<=121; fy++) EPD_DrawLine(0, fy, 249, fy, BLACK);
    EPD_ShowString(2,   112, "CLK=CONFIRM", WHITE, 12);
    EPD_ShowString(180, 112, "EXIT=CANCEL", WHITE, 12);

    EPD_DisplayImage(ImageBW);
    EPD_PartUpdate();
    EPD_Sleep();
}

// ── Parse FROW ────────────────────────────────────────────────────────────────
void parseFROW(const char* line) {
    if (fmRowCount >= FM_MAX_ROWS) return;
    char buf[100]; strncpy(buf, line, 99); buf[99]='\0';
    char* f[8]={nullptr}; int fi=0;
    char* tok=strtok(buf,",");
    while (tok && fi<8) { f[fi++]=tok; tok=strtok(nullptr,","); }
    if (fi < 7) return;
    CsvRow& r = fmRows[fmRowCount];
    r.count = atoi(f[0]);
    strncpy(r.date,  f[1], 11); r.date[11]='\0';
    strncpy(r.time,  f[2], 9);  r.time[9] ='\0';
    strncpy(r.fix,   (strncmp(f[3],"NO",2)==0) ? "NOFX" : "FIX", 7);
    strncpy(r.surfT, f[5], 7);  r.surfT[7]='\0';
    strncpy(r.depT,  f[6], 7);  r.depT[7] ='\0';
    fmRowCount++;
}

void requestFLIST() {
    fmRowCount=0; fmScroll=0; fmSelected=0;
    fmLoading=true;
    drawFM_Loading();
    mainSerial.println("FLIST");
    Serial.println("[FM] Sent FLIST");
}

void enterFileManager() {
    screenMode = MODE_FILEMANAGER;
    fmWaitingReply = false;
    requestFLIST();
}

void exitFileManager() {
    screenMode = MODE_MAIN;
    fmLoading = fmWaitingReply = false;
    drawMainScreen();
}

// ── Parse UART ────────────────────────────────────────────────────────────────
void parseMessage(const char* msg) {

    if (strncmp(msg,"DISP,",5)==0) {
        char buf[80]; strncpy(buf,msg+5,79); buf[79]='\0';
        char* f[6]={nullptr}; int fi=0;
        char* tok=strtok(buf,",");
        while (tok&&fi<6) { f[fi++]=tok; tok=strtok(nullptr,","); }
        if (fi<6) return;
        int  tNumA  = atoi(f[0]);
        bool tFullA = (strcmp(f[1],"FULL")==0);
        int  tNumB  = atoi(f[2]);
        bool tFullB = (strcmp(f[3],"FULL")==0);
        bool tFix   = (strcmp(f[4],"FIXED")==0);
        char tName[32]; strncpy(tName,f[5],31); tName[31]='\0';
        int nl=strlen(tName);
        while (nl>0&&(tName[nl-1]=='\r'||tName[nl-1]=='\n')) tName[--nl]='\0';
        bool changed = !hasData
            || tNumA!=numA || tFullA!=fullA
            || tNumB!=numB || tFullB!=fullB
            || tFix!=gpsFix || strcmp(tName,csvName)!=0;
        numA=tNumA; fullA=tFullA; numB=tNumB; fullB=tFullB;
        gpsFix=tFix; strncpy(csvName,tName,31); hasData=true;
        Serial.printf("[SCREEN] A=%d/%s B=%d/%s GPS=%s%s\n",
            numA,fullA?"F":"E",numB,fullB?"F":"E",
            gpsFix?"FIX":"NOFX",changed?" →REDRAW":"");
        if (changed && screenMode==MODE_MAIN) drawMainScreen();
        return;
    }

    if (strncmp(msg,"FROW,",5)==0)  { parseFROW(msg+5); return; }

    if (strncmp(msg,"FDONE,",6)==0) {
        fmLoading=false;
        Serial.printf("[FM] FDONE %d rows\n", fmRowCount);
        if (screenMode==MODE_FILEMANAGER) drawFM_Table();
        return;
    }

    if (strcmp(msg,"FDEL_OK")==0) {
        fmWaitingReply=false;
        screenMode=MODE_FILEMANAGER;
        requestFLIST();
        return;
    }
    if (strcmp(msg,"FDEL_ERR")==0) {
        fmWaitingReply=false;
        screenMode=MODE_FILEMANAGER;
        drawFM_Table();
        return;
    }
    if (strcmp(msg,"FWIPE_OK")==0) {
        fmWaitingReply=false;
        mainSerial.println("POLL");
        screenMode=MODE_FILEMANAGER;
        requestFLIST();
        return;
    }
    if (strcmp(msg,"FWIPE_ERR")==0) {
        fmWaitingReply=false;
        screenMode=MODE_FILEMANAGER;
        drawFM_Table();
        return;
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    pinMode(7, OUTPUT); digitalWrite(7, HIGH);
    pinMode(PIN_MENU,      INPUT_PULLUP);
    pinMode(PIN_EXIT,      INPUT_PULLUP);
    pinMode(PIN_ROT_DOWN,  INPUT_PULLUP);
    pinMode(PIN_ROT_UP,    INPUT_PULLUP);
    pinMode(PIN_ROT_CLICK, INPUT_PULLUP);
    mainSerial.begin(9600, SERIAL_8N1, 44, 43);
    drawMainScreen();
    Serial.println("[SCREEN] v6 Ready");
}

char    inBuf[100];
uint8_t inPos=0;

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {

    // Poll main for DISP updates
    if (screenMode==MODE_MAIN) {
        if (millis()-lastPoll>=POLL_INTERVAL) { lastPoll=millis(); mainSerial.println("POLL"); }
    }

    // Read UART
    while (mainSerial.available()) {
        char c=mainSerial.read();
        if (c=='\n'||c=='\r') {
            if (inPos>0) { inBuf[inPos]='\0'; parseMessage(inBuf); inPos=0; }
        } else { if (inPos<sizeof(inBuf)-1) inBuf[inPos++]=c; else inPos=0; }
    }

    // Buttons
    bool pressMenu  = edgeDetect(btnMenu);
    bool pressExit  = edgeDetect(btnExit);
    bool pressDown  = edgeDetect(btnRotDown);
    bool pressUp    = edgeDetect(btnRotUp);
    bool pressClick = edgeDetect(btnRotClick);

    // Long-press rotary click (>1.5s) → wipe confirm
    static unsigned long clickHeldAt=0;
    static bool clickWasDown=false;
    bool clickRaw=(digitalRead(PIN_ROT_CLICK)==LOW);
    if (clickRaw && !clickWasDown) { clickHeldAt=millis(); clickWasDown=true; }
    else if (!clickRaw)             { clickWasDown=false; }
    bool longPress = clickRaw && clickWasDown
                  && screenMode==MODE_FILEMANAGER
                  && !fmWaitingReply
                  && (millis()-clickHeldAt > 1500);

    // ── MODE_MAIN ─────────────────────────────────────────────────────────────
    if (screenMode==MODE_MAIN) {
        if (pressMenu) enterFileManager();
        return;
    }

    // Don't process buttons while waiting for a UART reply
    if (fmWaitingReply) return;

    // ── MODE_FILEMANAGER ──────────────────────────────────────────────────────
    if (screenMode==MODE_FILEMANAGER) {
        if (fmLoading) return;   // still loading, ignore buttons

        if (pressExit || pressMenu) { exitFileManager(); return; }

        if (pressDown && fmRowCount>0) {
            fmSelected=min(fmSelected+1, fmRowCount-1);
            if (fmSelected >= fmScroll+FM_VISIBLE) fmScroll=fmSelected-FM_VISIBLE+1;
            drawFM_Table(); return;
        }
        if (pressUp && fmRowCount>0) {
            fmSelected=max(fmSelected-1, 0);
            if (fmSelected < fmScroll) fmScroll=fmSelected;
            drawFM_Table(); return;
        }
        if (pressClick && fmRowCount>0) {
            clickWasDown=false;
            screenMode=MODE_CONFIRM_DEL;
            drawFM_ConfirmDelete(); return;
        }
        if (longPress) {
            clickWasDown=false;
            screenMode=MODE_CONFIRM_WIPE;
            drawFM_ConfirmWipe(); return;
        }
        return;
    }

    // ── MODE_CONFIRM_DEL ──────────────────────────────────────────────────────
    if (screenMode==MODE_CONFIRM_DEL) {
        if (pressClick) {
            char cmd[20];
            snprintf(cmd, sizeof(cmd), "FDEL,%d", fmRows[fmSelected].count);
            mainSerial.println(cmd);
            fmWaitingReply=true;
            Serial.printf("[FM] Sent %s\n", cmd);
        } else if (pressExit || pressMenu) {
            screenMode=MODE_FILEMANAGER;
            drawFM_Table();
        }
        return;
    }

    // ── MODE_CONFIRM_WIPE ─────────────────────────────────────────────────────
    if (screenMode==MODE_CONFIRM_WIPE) {
        if (pressClick) {
            mainSerial.println("FWIPE");
            fmWaitingReply=true;
            Serial.println("[FM] Sent FWIPE");
        } else if (pressExit || pressMenu) {
            screenMode=MODE_FILEMANAGER;
            drawFM_Table();
        }
        return;
    }
}