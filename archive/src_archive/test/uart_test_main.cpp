// uart_test_main.cpp
// Sends "Hello World" to display every 2 seconds over UART2 (D8=TX, D9=RX)
// Upload to Arduino Nano ESP32 with: build_src_filter = -<*> +<uart_test_main.cpp>

#include <Arduino.h>

HardwareSerial dispSerial(2);   // UART2 via GPIO matrix on D8(TX)/D9(RX)

const int DISP_TX_PIN = D8;
const int DISP_RX_PIN = D9;

void setup() {
  Serial.begin(115200);
  delay(2000);
  dispSerial.begin(9600, SERIAL_8N1, DISP_RX_PIN, DISP_TX_PIN);
  Serial.println("[TEST] UART2 ready — sending every 2s");
}

void loop() {
  dispSerial.println("Hello World");
  Serial.println("[TEST] Sent: Hello World");

  while (dispSerial.available()) {
    char c = dispSerial.read();
    Serial.print("[RX] ");
    Serial.println(c);
  }

  delay(2000);
}
