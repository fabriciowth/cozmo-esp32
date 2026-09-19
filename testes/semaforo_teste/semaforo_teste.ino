// Teste puro dos 3 LEDs do semaforo. Nada de WiFi, OLED ou som.
const uint8_t LEDS[3] = { 26, 33, 32 };   // verde, amarelo, vermelho
const char *NOME[3]   = { "VERDE(26)", "AMARELO(33)", "VERMELHO(32)" };

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 3; i++) { pinMode(LEDS[i], OUTPUT); digitalWrite(LEDS[i], LOW); }
}

void loop() {
  for (uint8_t i = 0; i < 3; i++) {
    Serial.println(NOME[i]);
    digitalWrite(LEDS[i], HIGH);
    delay(1000);
    digitalWrite(LEDS[i], LOW);
    delay(300);
  }
  Serial.println("--- todos juntos ---");
  for (uint8_t i = 0; i < 3; i++) digitalWrite(LEDS[i], HIGH);
  delay(1000);
  for (uint8_t i = 0; i < 3; i++) digitalWrite(LEDS[i], LOW);
  delay(1000);
}
