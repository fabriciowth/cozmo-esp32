// Diagnostico de WiFi: varre as redes e tenta conectar, dizendo o motivo exato da falha.
#include <WiFi.h>

#define WIFI_SSID "SUA-REDE-2.4GHZ"
#define WIFI_PASS "SUA-SENHA"

// motivos mais comuns (padrao 802.11 + extensoes do ESP32)
const char *motivo(int r) {
  switch (r) {
    case 2:   return "senha errada (autenticacao expirou)";
    case 15:  return "SENHA ERRADA (handshake falhou)";
    case 201: return "REDE NAO ENCONTRADA (5GHz? nome errado? longe?)";
    case 202: return "falha de autenticacao";
    case 203: return "associacao recusada";
    case 205: return "conexao perdida";
    default:  return "outro";
  }
}

void onEvent(WiFiEvent_t e, WiFiEventInfo_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    int r = info.wifi_sta_disconnected.reason;
    Serial.printf(">> desconectou, reason=%d : %s\n", r, motivo(r));
  } else if (e == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.println(">> associou no ponto de acesso");
  } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf(">> IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== DIAGNOSTICO WIFI ===");

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(600);

  Serial.println("\n1) varrendo redes 2.4GHz (inclui ocultas)...");
  int n = WiFi.scanNetworks(false, true);
  Serial.printf("   encontradas: %d\n", n);
  bool achou = false;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    bool eh = (s == WIFI_SSID);
    if (eh) achou = true;
    Serial.printf("   %s%-28s %4d dBm  ch%-3d %s\n",
                  eh ? "-> " : "   ",
                  s.length() ? s.c_str() : "(oculta)",
                  WiFi.RSSI(i), WiFi.channel(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "aberta" : "com senha");
  }
  Serial.printf("\n   \"%s\" %s na varredura\n", WIFI_SSID,
                achou ? "APARECE" : "NAO APARECE");

  Serial.println("\n2) tentando conectar...");
  WiFi.onEvent(onEvent);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
  static uint32_t t = 0;
  if (millis() - t > 3000) {
    t = millis();
    Serial.printf("   status=%d %s\n", WiFi.status(),
                  WiFi.status() == WL_CONNECTED ? "CONECTADO" : "");
  }
}
