// uart_test_display.cpp
// Listens on UART1 (RX=GPIO50, TX=GPIO49) and prints anything received to USB serial

#include <Arduino.h>

HardwareSerial mainSerial(1);

void setup() {
  Serial.begin(115200);
  delay(1000);
  mainSerial.begin(9600, SERIAL_8N1, 50, 49);  // RX=GPIO50, TX=GPIO49
  Serial.println("[TEST] ready — waiting for data on GPIO50");
}

char inBuf[64];
uint8_t inPos = 0;

void loop() {
  while (mainSerial.available()) {
    char c = mainSerial.read();
    Serial.print(c);

    if (c == '\n' || c == '\r') {
      if (inPos > 0) {
        inBuf[inPos] = '\0';
        Serial.print("[RX] "); Serial.println(inBuf);
        mainSerial.println("ACK");
        Serial.println("[TX] Sent ACK");
        inPos = 0;
      }
    } else {
      if (inPos < sizeof(inBuf) - 1) inBuf[inPos++] = c;
      else inPos = 0;
    }
  }
}
