// Descobre se o servo e de posicao (0-180) ou de rotacao continua (360).
#include <ESP32Servo.h>
Servo s;

void setup() {
  Serial.begin(115200);
  s.setPeriodHertz(50);
  s.attach(13, 500, 2400);
}

void loop() {
  Serial.println("write(90)  -> POSICAO: vai pro meio e PARA | CONTINUO: fica parado");
  s.write(90);  delay(3000);

  Serial.println("write(0)   -> POSICAO: vai pra uma ponta e PARA | CONTINUO: gira sem parar");
  s.write(0);   delay(3000);

  Serial.println("write(90)  -> para");
  s.write(90);  delay(2000);

  Serial.println("write(180) -> POSICAO: outra ponta e PARA | CONTINUO: gira pro outro lado");
  s.write(180); delay(3000);

  Serial.println("write(90)  -> para\n");
  s.write(90);  delay(2000);
}
