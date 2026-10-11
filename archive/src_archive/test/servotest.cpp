#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

const uint8_t CH_S1 = 2, CH_S2 = 1, CH_S3 = 0, CH_S4 = 3;

int currentUS = 1470;

void setup() {
    Serial.begin(115200);
    Wire.begin();
    pwm.begin();
    pwm.setOscillatorFrequency(29000000);
    pwm.setPWMFreq(50);
    delay(10);

    Serial.println("[SWEEP] Starting stop sweep 1450→1550, 3s each");
}

void loop() {
    if (currentUS > 1510) {
        Serial.println("[SWEEP] Done.");
        while(true) delay(1000);
    }

    Serial.printf("[SWEEP] Writing %d us to all channels\n", currentUS);
    pwm.writeMicroseconds(CH_S1, currentUS);
    pwm.writeMicroseconds(CH_S2, currentUS);
    pwm.writeMicroseconds(CH_S3, currentUS);
    pwm.writeMicroseconds(CH_S4, currentUS);

    delay(3000);
    currentUS++;
}