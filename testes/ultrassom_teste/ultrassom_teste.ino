// Diagnostico do HC-SR04: separa problema de alimentacao, de divisor e de medida.
#include <Wire.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

#define TRIG 18
#define ECHO 19

void setup() {
  Serial.begin(115200);
  delay(1200);
  pinMode(TRIG, OUTPUT);
  digitalWrite(TRIG, LOW);
  pinMode(ECHO, INPUT);
  Wire.begin();
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);
  Serial.println("\n=== TESTE HC-SR04 === aponte o sensor para a mesa, uns 15-30 cm");
}

void loop() {
  int repouso = digitalRead(ECHO);             // parado, o ECHO deve estar em LOW

  digitalWrite(TRIG, LOW);  delayMicroseconds(3);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  // acompanha o pino na mao em vez de pulseIn, para saber EM QUE PARTE falha
  unsigned long t0 = micros(), subiu = 0, desceu = 0;
  while (micros() - t0 < 40000) {
    int v = digitalRead(ECHO);
    if (!subiu && v)             subiu = micros();
    if (subiu && !v)           { desceu = micros(); break; }
  }

  if (repouso) {
    Serial.println("ECHO preso em HIGH  -> fio do ECHO/D19 encostando no VCC ou divisor ligado errado");
  } else if (!subiu) {
    Serial.println("ECHO nunca subiu    -> sensor sem 5V (VCC no 3V3?) OU divisor invertido (tensao baixa demais)");
  } else if (!desceu) {
    Serial.println("ECHO subiu e nao desceu em 40ms -> nada na frente (apontando pro vazio) ou mais de 6m");
  } else {
    unsigned long w = desceu - subiu;
    Serial.printf("OK  pulso %5lu us  =  %.1f cm\n", w, w / 58.3f);
  }
  // mesmo resultado no OLED, para testar ligado num carregador sem o notebook
  oled.clearDisplay();
  oled.setTextSize(1); oled.setCursor(0, 0); oled.print("TESTE HC-SR04");
  oled.setTextSize(2); oled.setCursor(0, 24);
  if (repouso)       oled.print("ECHO PRESO");
  else if (!subiu)   oled.print("SEM ECHO");
  else if (!desceu)  oled.print("SEM RETORNO");
  else             { oled.print((desceu - subiu) / 58.3f, 1); oled.print(" cm"); }
  static int ok = 0, total = 0;
  total++; if (subiu && desceu) ok++;
  oled.setTextSize(1); oled.setCursor(0, 54);
  oled.printf("acertos %d/%d", ok, total);
  oled.display();
  delay(400);
}
