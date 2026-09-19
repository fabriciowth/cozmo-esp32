// Copie este arquivo para secrets.h e preencha com os seus dados.
#pragma once

#define WIFI_SSID "SUA-REDE-2.4GHZ"
#define WIFI_PASS "SUA-SENHA"

#define OPENAI_KEY "sk-SUA-CHAVE-OPENAI"
#define OPENAI_MODEL "gpt-4o-mini"

// Deepgram: so transcricao. O Aura (voz deles) nao fala portugues,
// entao quem fala a resposta continua sendo o Android.
#define DEEPGRAM_KEY "SUA-CHAVE-DEEPGRAM"
#define DEEPGRAM_URL "https://api.deepgram.com/v1/listen" \
                     "?model=nova-2&language=pt-BR&smart_format=true&punctuate=true"

// Linhares - ES
#define CIDADE "Sua-Cidade"
#define LAT "-19.39"
#define LON "-40.07"
