#include <ESP32Servo.h>
#include <TinyGPSPlus.h>
#include <DS18B20.h>
#include <SD.h>
#include <SPI.h>

// ── Pins ──────────────────────────────────────
const int SD_CS_PIN          = D10;
const int RECEIVER_PIN       = D6;
const int SERVO_SURFACE1_PIN = A0;
const int SERVO_DEPTH1_PIN   = A1;
const int SERVO_SURFACE2_PIN = D5;
const int SERVO_DEPTH2_PIN   = D2;
const int MAG_SURFACE1       = A4;
const int MAG_DEPTH1         = A5;
const int MAG_SURFACE2       = A6;
const int MAG_DEPTH2         = A7;

// ── Servo timing ──────────────────────────────
const int STOP  = 1510;
const int OPEN  = 1000;
const int CLOSE = 2000;

// ── Servo objects ─────────────────────────────
Servo servoSurf1, servoDepth1, servoSurf2, servoDepth2;

// ── Timing ────────────────────────────────────
const unsigned long PAUSE_TIME      = 2000;
const unsigned long MAG_IGNORE_TIME = 2000;
const unsigned long MAX_MOVE_TIME   = 15000;
const unsigned long LED_FLASH_MS    = 150;
const uint32_t      TRIGGER_THRESHOLD = 1800;

// ── Hardware ──────────────────────────────────
DS18B20 ds(8);
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
const uint8_t SURFACE_ADDR[8] = {40, 156, 235, 111, 0, 0, 0, 114};
const uint8_t DEPTH_ADDR[8]   = {40, 122, 122, 116, 0, 0, 0,  63};

// ── State ─────────────────────────────────────
int surfMode=0; unsigned long surfPrev=0;
int depthMode=0; unsigned long depthPrev=0;
Servo *pSurf  = &servoSurf1;
Servo *pDepth = &servoDepth1;
int surfReed = MAG_SURFACE1;
int depthReed= MAG_DEPTH1;
int pairIndex=0;

int           sampleCount      = 0;
unsigned long lastHeartbeat    = 0;
unsigned long ledGreenOffAt    = 0;
unsigned long ledRedOffAt      = 0;
unsigned long ledFlashNext     = 0;
bool          ledRedState      = false;
bool          lastReceiverHigh = false;
unsigned long lastTriggerAt    = 0;
bool          fixAnnounced     = false;
char          logFilename[20]  = "/nodate.csv";
float         pendingSurf = NAN, pendingDepth = NAN;

struct GpsSnap { bool valid, hasFix; char date[12]; double lat, lng; };
GpsSnap snap = {};

// ── GPS / SD / LED helpers ─────────────────────
void feedGPS(unsigned long ms) {
  unsigned long s = millis();
  while (millis() - s < ms) while (gpsSerial.available()) gps.encode(gpsSerial.read());
}
static int daysInMonth(int m, int y) {
  const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m==2 && ((y%4==0&&y%100!=0)||y%400==0)) return 29; return d[m-1];
}
static int dayOfWeek(int y, int m, int d) {
  static const int t[]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<3)y--;
  return (y+y/4-y/100+y/400+t[m-1]+d)%7;
}
static int nthWeekday(int n,int dow,int m,int y){int f=dayOfWeek(y,m,1);return 1+((dow-f+7)%7)+(n-1)*7;}
static int getOffset(int Y,int M,int D,int H){
  int s=nthWeekday(2,0,3,Y),e=nthWeekday(1,0,11,Y);
  bool a=(M>3)||(M==3&&D>s)||(M==3&&D==s&&H>=7);
  bool b=(M<11)||(M==11&&D<e)||(M==11&&D==e&&H<6);
  return (a&&b)?-4:-5;
}
bool localComponents(int*yr,int*mo,int*dy,int*hr,int*mn,int*sc){
  if(!gps.date.isValid()||!gps.time.isValid())return false;
  int u=(int)gps.time.hour(),off=getOffset((int)gps.date.year(),(int)gps.date.month(),(int)gps.date.day(),u);
  *hr=u+off;*mn=(int)gps.time.minute();*sc=(int)gps.time.second();
  *yr=(int)gps.date.year();*mo=(int)gps.date.month();*dy=(int)gps.date.day();
  if(*hr<0){*hr+=24;(*dy)--;if(*dy<1){(*mo)--;if(*mo<1){*mo=12;(*yr)--;}*dy=daysInMonth(*mo,*yr);}}
  return true;
}
void dateStr(char*buf){int y,m,d,h,n,s;if(localComponents(&y,&m,&d,&h,&n,&s))sprintf(buf,"%02d/%02d/%04d",m,d,y);
  else if(gps.date.isValid())sprintf(buf,"%02d/%02d/%04d",gps.date.month(),gps.date.day(),gps.date.year());
  else strcpy(buf,"--/--/----");}
void estStr(char*buf){int y,m,d,h,n,s;if(localComponents(&y,&m,&d,&h,&n,&s))sprintf(buf,"%02d:%02d:%02d",h,n,s);
  else strcpy(buf,"--:--:--");}
void initLogFile(const char*fn){
  strcpy(logFilename,fn);
  if(!SD.exists(logFilename)){File f=SD.open(logFilename,FILE_WRITE);if(f){f.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C");f.close();}sampleCount=0;}
  else{File f=SD.open(logFilename,FILE_READ);int l=0;if(f){while(f.available()){if(f.read()=='\n')l++;}f.close();}sampleCount=max(0,l-1);}
}
void writeSDRow(float st,float dt){
  if(!snap.valid)return; snap.valid=false;
  if(!SD.exists(logFilename)){File h=SD.open(logFilename,FILE_WRITE);if(h){h.println("Count,Date,Time_EST,Lat,Long,SurfaceTemp_C,DepthTemp_C");h.close();}else return;}
  File f=SD.open(logFilename,FILE_APPEND);if(!f)return;
  float sv=isnan(st)?0:st,dv=isnan(dt)?0:dt; char t[10];estStr(t);
  f.print(sampleCount);f.print(",");f.print(snap.date);f.print(",");f.print(t);f.print(",");
  if(snap.hasFix){f.print(snap.lat,6);f.print(",");f.print(snap.lng,6);}else f.print("NO FIX,NO FIX");
  f.print(",");f.print(sv,2);f.print(",");f.println(dv,2);f.close();
}
void ledSet(int pin,bool on){digitalWrite(pin,on?LOW:HIGH);}
void readTemps(float&st,float&dt){
  st=NAN;dt=NAN;
  while(ds.selectNext()){uint8_t a[8];ds.getAddress(a);float c=ds.getTempC();if(fabsf(c-85)<0.5f)c=NAN;
    if(memcmp(a,SURFACE_ADDR,8)==0)st=c;else if(memcmp(a,DEPTH_ADDR,8)==0)dt=c;}
}

// ── Setup ─────────────────────────────────────
void setup(){
  Serial.begin(115200); delay(2000);

  servoSurf1.setPeriodHertz(50);  servoSurf1.attach(SERVO_SURFACE1_PIN);
  servoDepth1.setPeriodHertz(50); servoDepth1.attach(SERVO_DEPTH1_PIN);
  servoSurf2.setPeriodHertz(50);  servoSurf2.attach(SERVO_SURFACE2_PIN);
  servoDepth2.setPeriodHertz(50); servoDepth2.attach(SERVO_DEPTH2_PIN);
  servoSurf1.writeMicroseconds(STOP);
  servoDepth1.writeMicroseconds(STOP);
  servoSurf2.writeMicroseconds(STOP);
  servoDepth2.writeMicroseconds(STOP);

  pinMode(RECEIVER_PIN,INPUT);
  pinMode(MAG_SURFACE1,INPUT_PULLUP); pinMode(MAG_DEPTH1,INPUT_PULLUP);
  pinMode(MAG_SURFACE2,INPUT_PULLUP); pinMode(MAG_DEPTH2,INPUT_PULLUP);
  pinMode(LEDG,OUTPUT);ledSet(LEDG,false); pinMode(LEDR,OUTPUT);ledSet(LEDR,false); pinMode(LEDB,OUTPUT);ledSet(LEDB,false);
  SPI.begin(D13,D12,D11,D10);
  if(SD.begin(SD_CS_PIN)){Serial.println("[SD] ready");initLogFile("/nodate.csv");}else Serial.println("[SD] FAILED");
  delay(100); gpsSerial.begin(9600,SERIAL_8N1,D3,D4);
  Serial.print("[TEMP] Priming");while(ds.selectNext())ds.getTempC();delay(800);while(ds.selectNext())ds.getTempC();Serial.println(" done");

  // ── Pin diagnostic ────────────────────────────
  Serial.println("[PIN-TEST] surface1 (A0)");
  servoSurf1.writeMicroseconds(OPEN);  delay(2000); servoSurf1.writeMicroseconds(STOP);  delay(500);
  Serial.println("[PIN-TEST] depth1 (A1)");
  servoDepth1.writeMicroseconds(OPEN); delay(2000); servoDepth1.writeMicroseconds(STOP); delay(500);
  Serial.println("[PIN-TEST] surface2 (D5)");
  servoSurf2.writeMicroseconds(OPEN);  delay(2000); servoSurf2.writeMicroseconds(STOP);  delay(500);
  Serial.println("[PIN-TEST] depth2 (D2)");
  servoDepth2.writeMicroseconds(OPEN); delay(2000); servoDepth2.writeMicroseconds(STOP); delay(500);
  Serial.println("[PIN-TEST] done");
  // ─────────────────────────────────────────────

  Serial.println("[SYS] v2.6 Ready");
}

// ── Loop ──────────────────────────────────────
void loop(){
  while(gpsSerial.available()) gps.encode(gpsSerial.read());

  if(!fixAnnounced&&gps.location.isValid()&&gps.date.isValid()){
    fixAnnounced=true; int y,m,d,h,n,s; char fn[20];
    if(localComponents(&y,&m,&d,&h,&n,&s))sprintf(fn,"/%04d%02d%02d.csv",y,m,d);
    else sprintf(fn,"/%04d%02d%02d.csv",gps.date.year(),gps.date.month(),gps.date.day());
    if(strcmp(logFilename,fn)!=0)initLogFile(fn);
    Serial.println("[GPS] Fix");
  }

  uint32_t pulse    = pulseIn(RECEIVER_PIN,HIGH,25000);
  bool receiverHigh = (pulse > TRIGGER_THRESHOLD);
  bool bothIdle     = (surfMode==0 && depthMode==0);
  bool triggered    = receiverHigh && !lastReceiverHigh && bothIdle
                      && millis()-lastTriggerAt >= 1000;
  lastReceiverHigh  = receiverHigh;

  if(bothIdle && millis()-lastHeartbeat>=5000){
    lastHeartbeat=millis();
    Serial.printf("[IDLE] next=pair%d samples=%d\n", pairIndex+1, sampleCount);
  }

  if(triggered){
    lastTriggerAt=millis(); sampleCount++;
    feedGPS(100);
    snap.hasFix=gps.location.isValid(); snap.lat=snap.hasFix?gps.location.lat():0; snap.lng=snap.hasFix?gps.location.lng():0;
    dateStr(snap.date); snap.valid=true;
    readTemps(pendingSurf,pendingDepth);
    Serial.printf("[TRIG] Pair %d\n", pairIndex+1);
    if(pairIndex==0){
      pSurf=&servoSurf1; surfReed=MAG_SURFACE1;
      pDepth=&servoDepth1; depthReed=MAG_DEPTH1;
    } else {
      pSurf=&servoSurf2; surfReed=MAG_SURFACE2;
      pDepth=&servoDepth2; depthReed=MAG_DEPTH2;
    }
    surfMode=1; surfPrev=millis();
    depthMode=1; depthPrev=millis();
    pairIndex=1-pairIndex;
    ledSet(LEDG,true); ledGreenOffAt=millis()+1000; ledFlashNext=millis();
  }

  if(ledGreenOffAt&&millis()>=ledGreenOffAt){ledSet(LEDG,false);ledGreenOffAt=0;}
  if(!bothIdle&&ledRedOffAt==0&&millis()>=ledFlashNext){ledRedState=!ledRedState;ledSet(LEDR,ledRedState);ledFlashNext=millis()+LED_FLASH_MS;}
  if(ledRedOffAt&&millis()>=ledRedOffAt){ledSet(LEDR,false);ledRedOffAt=0;ledRedState=false;}

  // ── Surface machine ───────────────────────────
  switch(surfMode){
    case 0:
      servoSurf1.writeMicroseconds(STOP);
      servoSurf2.writeMicroseconds(STOP);
      break;
    case 1:
      pSurf->writeMicroseconds(OPEN);
      if(millis()-surfPrev >= MAG_IGNORE_TIME){
        if(digitalRead(surfReed)==LOW || millis()-surfPrev >= MAX_MOVE_TIME){
          pSurf->writeMicroseconds(STOP);
          Serial.printf("[SURF] open-stop val=%d elapsed=%lums\n",digitalRead(surfReed),millis()-surfPrev);
          writeSDRow(pendingSurf,pendingDepth);
          surfPrev=millis(); surfMode=2;
        }
      }
      break;
    case 2:
      pSurf->writeMicroseconds(STOP);
      if(millis()-surfPrev >= PAUSE_TIME){ surfPrev=millis(); surfMode=3; }
      break;
    case 3:
      pSurf->writeMicroseconds(CLOSE);
      if(millis()-surfPrev >= MAG_IGNORE_TIME){
        if(digitalRead(surfReed)==LOW || millis()-surfPrev >= MAX_MOVE_TIME){
          pSurf->writeMicroseconds(STOP);
          Serial.println(digitalRead(surfReed)==LOW?"[SURF] closed-reed":"[SURF] closed-timeout");
          ledSet(LEDR,true);ledRedState=true;ledRedOffAt=millis()+1000;ledFlashNext=0;
          surfMode=0;
        }
      }
      break;
  }

  // ── Depth machine ─────────────────────────────
  switch(depthMode){
    case 0:
      servoDepth1.writeMicroseconds(STOP);
      servoDepth2.writeMicroseconds(STOP);
      break;
    case 1:
      pDepth->writeMicroseconds(OPEN);
      if(millis()-depthPrev >= MAG_IGNORE_TIME){
        if(digitalRead(depthReed)==LOW || millis()-depthPrev >= MAX_MOVE_TIME){
          pDepth->writeMicroseconds(STOP);
          Serial.printf("[DEPTH] open-stop val=%d elapsed=%lums\n",digitalRead(depthReed),millis()-depthPrev);
          depthPrev=millis(); depthMode=2;
        }
      }
      break;
    case 2:
      pDepth->writeMicroseconds(STOP);
      if(millis()-depthPrev >= PAUSE_TIME){ depthPrev=millis(); depthMode=3; }
      break;
    case 3:
      pDepth->writeMicroseconds(CLOSE);
      if(millis()-depthPrev >= MAG_IGNORE_TIME){
        if(digitalRead(depthReed)==LOW || millis()-depthPrev >= MAX_MOVE_TIME){
          pDepth->writeMicroseconds(STOP);
          Serial.println(digitalRead(depthReed)==LOW?"[DEPTH] closed-reed":"[DEPTH] closed-timeout");
          ledSet(LEDR,true);ledRedState=true;ledRedOffAt=millis()+1000;ledFlashNext=0;
          depthMode=0;
        }
      }
      break;
  }

}
