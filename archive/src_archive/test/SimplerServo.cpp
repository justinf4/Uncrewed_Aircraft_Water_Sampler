#include <ESP32Servo.h>

Servo s1, s2, s3, s4;

const int STOP  = 1510;
const int OPEN  = 1000;
const int CLOSE = 2000;

void runPair(Servo& sA, int pinA, int reedA, const char* labelA,
             Servo& sB, int pinB, int reedB, const char* labelB) {

  Serial.print("Starting: "); Serial.print(labelA);
  Serial.print(" + ");        Serial.println(labelB);

  sA.attach(pinA, 1000, 2000); sA.writeMicroseconds(OPEN);
  delay(50);
  sB.attach(pinB, 1000, 2000); sB.writeMicroseconds(OPEN);

  bool stoppedA = false;
  bool stoppedB = false;

  while (!stoppedA || !stoppedB) {
    if (!stoppedA && digitalRead(reedA) == LOW) {
      Serial.print(labelA); Serial.println(" stop");
      sA.writeMicroseconds(STOP);
      stoppedA = true;
    }
    if (!stoppedB && digitalRead(reedB) == LOW) {
      Serial.print(labelB); Serial.println(" stop");
      sB.writeMicroseconds(STOP);
      stoppedB = true;
    }
  }

  delay(1500);
  sA.detach(); pinMode(pinA, OUTPUT); digitalWrite(pinA, LOW);
  sB.detach(); pinMode(pinB, OUTPUT); digitalWrite(pinB, LOW);
  delay(500);
}

void setup() {
  Serial.begin(115200);

  pinMode(A0, OUTPUT); digitalWrite(A0, LOW);
  pinMode(A1, OUTPUT); digitalWrite(A1, LOW);
  pinMode(D2, OUTPUT); digitalWrite(D2, LOW);
  pinMode(D5, OUTPUT); digitalWrite(D5, LOW);

  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
  pinMode(A6, INPUT_PULLUP);
  pinMode(A7, INPUT_PULLUP);

  delay(1000);
}

void loop() {
  runPair(s1, A0, A4, "A0",
          s2, A1, A5, "A1");

  runPair(s3, D2, A6, "D2",
          s4, D5, A7, "D5");
}