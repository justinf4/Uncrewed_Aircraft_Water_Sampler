
void setup() {

Serial.begin(9600);
pinMode(D6,INPUT);
}

void loop() {
uint32_t pulse = pulseIn(D6, HIGH);
Serial.println(pulse);
delay(300);
}
