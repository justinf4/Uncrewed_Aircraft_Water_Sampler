#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <dht11.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
const uint8_t CH_S1 = 2, CH_S2 = 1, CH_S3 = 0, CH_S4 = 3;

#define DHT11PIN D2
dht11 DHT11;

int currentUS = 1350;

void setup() {
    Serial.begin(115200);
    Wire.begin();
    pwm.begin();
    pwm.setOscillatorFrequency(29000000);
    pwm.setPWMFreq(50);
    delay(10);
    Serial.println("us,temp_C,humidity");
    Serial.println("[SWEEP] Starting — place in sun, note which us stops creep");
}

void loop() {
    if (currentUS > 1550) {
        Serial.println("[SWEEP] Done.");
        while(true) delay(1000);
    }

    DHT11.read(DHT11PIN);
    float temp = (float)DHT11.temperature;
    float hum  = (float)DHT11.humidity;

    Serial.printf("%d,%.1f,%.1f\n", currentUS, temp, hum);

    pwm.writeMicroseconds(CH_S1, currentUS);
    pwm.writeMicroseconds(CH_S2, currentUS);
    pwm.writeMicroseconds(CH_S3, currentUS);
    pwm.writeMicroseconds(CH_S4, currentUS);

    delay(1000);
    currentUS++;
}