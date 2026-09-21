// Cozmo de bancada com LLM: olhos animados, humor, e respostas faladas/escritas.
// Dados reais (hora, clima, cripto) vem de fontes proprias; o modelo so conversa.
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <ESP32Servo.h>

#define PIN_G   26
#define PIN_Y   33
#define PIN_R   32
#define SPK     25
#define BTN     27          // chave tactil entre o pino e o GND = "carinho"
#define MIC     15          // KY-037 saida DIGITAL DO
#define MIC_DEBOUNCE 250
#define SSD_ADDR 0x3C

#include "secrets.h"   // copie secrets.example.h e preencha

#define PIN_SERVO   14      // servo continuo (braco). Nao use 34-39: sao so entrada
#define SERVO_NEUTRO 90     // pulso em que ele fica parado; ajuste se ficar girando devagar
#define SERVO_VEL    35     // quanto afasta do neutro nos movimentos

// OLED azul/amarelo: linhas 0-15 amarelas, 16-63 azuis. O rosto fica so no azul.
#define FACE_TOP    17
#define FACE_CY     40
#define FACE_MAXH   44

#define FRAME_MS    40
#define SLEEP_MS 60000      // sem interacao por 60s -> dorme
#define REACT_MS  2200
#define ANIM_MS   1300      // quanto ele "atua" antes de mostrar o texto
#define TEXT_MS  10000      // quanto o texto fica na tela
#define CRYPTO_TTL  60000   // cotacao vale 1 min
#define WX_TTL     600000   // clima vale 10 min

enum Mood { NEUTRO, FELIZ, TRISTE, BRAVO, SONO, CURIOSO, SURPRESO, PENSANDO, NMOODS };
const char *MOOD_NAME[] = { "neutro","feliz","triste","bravo","sono","curioso","surpreso","pensando" };

enum { W, H, R, DY, TT, TO, HR, DYR, NP };
const float LOOKS[NMOODS][NP] = {
  { 36, 40, 10,  0,  0,  0, 1.00,  0 },   // NEUTRO
  { 36, 24, 12, -5,  0,  0, 1.00,  0 },   // FELIZ
  { 34, 30, 10,  7,  0, 14, 1.00,  0 },   // TRISTE
  { 38, 34,  8,  0, 16,  0, 1.00,  0 },   // BRAVO
  { 36,  4,  2,  9,  0,  0, 1.00,  0 },   // SONO
  { 36, 40, 10, -2,  0,  0, 0.65,  7 },   // CURIOSO
  { 44, 48, 14, -2,  0,  0, 1.00,  0 },   // SURPRESO
  { 30, 26,  9, -7,  0,  0, 0.85, -2 },   // PENSANDO - olhando pra cima
};

const int SFX[NMOODS][4][2] = {
  { {700,70},{0,0},{0,0},{0,0} },
  { {700,80},{900,80},{1200,120},{0,0} },
  { {600,140},{450,200},{0,0},{0,0} },
  { {300,90},{240,90},{300,90},{240,160} },
  { {500,180},{380,260},{0,0},{0,0} },
  { {900,70},{1150,90},{0,0},{0,0} },
  { {1400,60},{1800,60},{1400,60},{0,0} },
  { {820,60},{0,120},{820,60},{0,0} },              // PENSANDO - dois toques
};

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Servo arm;
struct Step { int8_t d; uint16_t ms; };   // no topo: a IDE poe os prototipos antes das funcoes

// ---- pedra, papel e tesoura ----
enum RpsPhase { RPS_OFF, RPS_COUNT, RPS_REVEAL, RPS_RESULT };
RpsPhase rps = RPS_OFF;
int rpsPick = 0, rpsUser = 0, rpsOutcome = 0;   // outcome: 1 cozmo ganhou, -1 voce, 0 empate
int rpsStep = -1, scoreCozmo = 0, scoreVoce = 0;
uint32_t tRps = 0;
const char *RPS_NAME[] = { "pedra", "papel", "tesoura" };
const char *RPS_UP[]   = { "PEDRA", "PAPEL", "TESOURA" };
#define RPS_STEP_MS   800     // duracao de cada numero da contagem
#define RPS_WAIT_MS 30000     // quanto espera voce dizer sua jogada
#define RPS_SHOW_MS  6000     // quanto o resultado fica na tela

// ---- copo d'agua (HC-SR04 apontado para baixo) ----
#define US_TRIG   18
#define US_ECHO   19          // pelo divisor de tensao! o ECHO sai 5V
#define CUP_MOUNT_CM   3.0    // distancia do sensor ate a BORDA do copo (meça e ajuste)
#define CUP_FULL_PCT    85    // quando considera cheio (deixa espaco pra nao derramar)
#define CUP_OVER_PCT    98    // quando avisa que vai transbordar
#define CUP_CONFIRM      4    // leituras seguidas acima do limite (ignora o fio de agua)
#define CUP_TIMEOUT 180000    // desiste depois de 3 min
enum CupPhase { CUP_OFF, CUP_FILL, CUP_FULL, CUP_OVER };
CupPhase cup = CUP_OFF;
float cupEmpty = 0, cupDepth = 0, cupRing[5];
int cupN = 0, cupPct = 0, cupHits = 0;
uint32_t tCup = 0, tCupPing = 0, tCupBeep = 0;
WebServer web(80);

Mood mood = NEUTRO;
float cur[NP], tgt[NP];
bool autoMode = true;
volatile uint32_t micLast = 0;
uint32_t tFrame = 0, tBlink = 0, tTouch = 0, tReact = 0, tSfx = 0, tAnsw = 0;
uint32_t talkUntil = 0;     // enquanto millis() < talkUntil, ele esta falando
int8_t   lightMask = -1;    // -1 = luzes seguem o humor; senao bits: 1 verde, 2 amarelo, 4 vermelho
bool     lightBlink = false;
uint32_t holdUntil = 0;     // expressao pedida por voz: ouvir/pensar nao sobrescreve
uint16_t blinkDur = 0;
int sfxIdx = -1;
String myIP;

// ---- conversa (trocado entre o loop e a tarefa de rede) ----
volatile bool askPending = false, thinking = false, answerReady = false;
String pendingQ, answer, lastQ;

// ---- dados reais ----
float btcBrl = 0, ethBrl = 0, tempC = -999;
uint32_t tCrypto = 0, tWx = 0;

void IRAM_ATTR onMicEdge() {
  uint32_t t = millis();
  if (t - micLast > MIC_DEBOUNCE) micLast = t;
}

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Cozmo</title><style>
body{margin:0;background:#0b0b0b;color:#eee;font:600 16px system-ui;text-align:center}
h1{font-size:18px;margin:12px}
#ans{margin:10px 14px;padding:14px;background:#1b1b1b;border-radius:14px;min-height:22px;
     font-weight:400;line-height:1.35}
.row{display:flex;gap:8px;padding:0 14px}
#q{flex:1;padding:14px;border:0;border-radius:12px;background:#222;color:#eee;font:400 16px system-ui}
#go,#voz{padding:14px 16px;border:0;border-radius:12px;font:700 16px system-ui}
#go{background:#22c55e}#voz{background:#3b82f6;color:#fff}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:12px 14px}
.grid button{padding:14px 0;border:0;border-radius:12px;background:#333;color:#eee;
             font:600 14px system-ui}
.moods{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;padding:0 14px 30px}
.moods button{padding:12px 0;border:0;border-radius:12px;font:700 13px system-ui;color:#000}
#m1{background:#22c55e}#m3{background:#ef4444}#m2{background:#60a5fa}
#m5{background:#eab308}#m6{background:#f97316}#m4{background:#7c3aed;color:#fff}
</style>
<h1 id=h>...</h1>
<div id=ans>pergunte alguma coisa</div>
<div class=row>
  <input id=q placeholder="fala com ele...">
  <button id=voz>&#127908;</button>
  <button id=go>&#9654;</button>
</div>
<div style="padding:10px 14px 0">
  <button id=conv style="width:100%;padding:18px;border:0;border-radius:14px;
          background:#a855f7;color:#fff;font:800 18px system-ui">CONVERSAR</button>
</div>
<div class=grid>
  <button data-q="que horas sao?">que horas sao</button>
  <button data-q="quanto esta o bitcoin?">bitcoin</button>
  <button data-q="qual a temperatura agora?">temperatura</button>
  <button data-q="me conta uma curiosidade curta">curiosidade</button>
  <button onclick="esp('/arm?a=wave')">&#128075; tchau</button>
  <button onclick="esp('/arm?a=dance')">&#128131; dancar</button>
</div>
<div class=moods>
  <button id=m1>FELIZ</button><button id=m3>BRAVO</button><button id=m2>TRISTE</button>
  <button id=m5>CURIOSO</button><button id=m6>SURPRESO</button><button id=m4>SONO</button>
</div>
<script>
let spoken='';
async function req(u){try{return await(await fetch(u)).json()}catch(e){return null}}
function fala(t){
  try{const u=new SpeechSynthesisUtterance(t);u.lang='pt-BR';u.rate=1.05;
      speechSynthesis.cancel();speechSynthesis.speak(u)}catch(e){}
}
function draw(d){
  if(!d){h.textContent='sem conexao';return}
  h.textContent=d.think?'pensando...':d.mood.toUpperCase();
  cupWatch(d);
  if(d.ans&&d.ans!==spoken){spoken=d.ans;if(!agentOn){ans.textContent=d.ans;fala(d.ans)}}
  else if(d.think)ans.textContent='...';
}
async function ask(t){
  if(!t)return;
  ans.textContent='...';q.value='';
  await req('/ask?q='+encodeURIComponent(t));
}
go.onclick=()=>ask(q.value);
q.addEventListener('keydown',e=>{if(e.key==='Enter')ask(q.value)});
document.querySelectorAll('[data-q]').forEach(b=>b.onclick=()=>ask(b.dataset.q));
for(let i=1;i<7;i++){
  const b=document.getElementById('m'+i);
  if(b)b.onclick=async()=>draw(await req('/mood?m='+i));
}
let dgKey='',dgUrl='',rec=null,chunks=[],gravando=false;
(async()=>{try{dgKey=await(await fetch('/dgkey')).text();
                dgUrl=await(await fetch('/dgurl')).text()}catch(e){}})();

async function transcrever(blob){
  ans.textContent='transcrevendo...';
  try{
    const r=await fetch(dgUrl,{method:'POST',
      headers:{'Authorization':'Token '+dgKey,'Content-Type':blob.type||'audio/webm'},
      body:blob});
    const j=await r.json();
    const t=j?.results?.channels?.[0]?.alternatives?.[0]?.transcript||'';
    if(!t.trim()){ans.textContent='nao entendi nada';return}
    ans.textContent='\u201c'+t+'\u201d';
    ask(t);
  }catch(e){ans.textContent='falhou ao transcrever'}
}

async function gravar(){
  // getUserMedia so existe em contexto seguro; http em IP local nao e um
  if(!navigator.mediaDevices?.getUserMedia){
    ans.innerHTML='o navegador bloqueia o microfone em http.<br><br>'+
      '1) abra <b>chrome://flags</b><br>'+
      '2) procure <b>Insecure origins treated as secure</b><br>'+
      '3) <b>DIGITE NA CAIXA DE TEXTO</b> (so mudar para Ativado nao basta):'+
      '<br><br><b style="font-size:17px">http://'+location.host+'</b><br><br>'+
      '4) deixe em Ativado e feche o Chrome pelos apps recentes';
    return;
  }
  try{
    const st=await navigator.mediaDevices.getUserMedia({audio:true});
    rec=new MediaRecorder(st);chunks=[];
    rec.ondataavailable=e=>chunks.push(e.data);
    rec.onstop=()=>{st.getTracks().forEach(t=>t.stop());
                    transcrever(new Blob(chunks,{type:rec.mimeType}))};
    rec.start();gravando=true;
    voz.style.background='#ef4444';voz.textContent='\u23f9';
    ans.textContent='ouvindo... toque para parar';
    setTimeout(()=>{if(gravando)parar()},8000);   // trava de seguranca
  }catch(e){ans.textContent='sem permissao pro microfone'}
}
function parar(){
  gravando=false;
  voz.style.background='#3b82f6';voz.textContent='\ud83c\udfa4';
  if(rec&&rec.state!=='inactive')rec.stop();
}
voz.onclick=()=>gravando?parar():gravar();
setInterval(async()=>draw(await req('/state')),600);

// ================= VOICE AGENT (Deepgram) =================
// O celular fala direto com o Deepgram por um WebSocket: ouve, pensa (gpt-4o-mini
// gerenciado por eles) e responde com a voz da OpenAI. O ESP32 so recebe cara e texto.
let agentOn=false,ws=null,ctxIn=null,ctxOut=null,micStream=null,proc=null;
let lastCup='';
// trava do WhatsApp: so envia se o usuario FALOU depois do rascunho, e falou confirmando
let lastUserAt=0,lastUserText='',waDraftAt=0;
const WA_SIM=/\b(pode|envia|enviar|manda|mandar|sim|confirmo|confirma|isso)\b/i;
const WA_NAO=/\b(n[a\u00e3]o|cancela|espera|pera|muda|troca|errado)\b/i;
async function ponte(path,body){
  const B='http://127.0.0.1:8765';
  try{
    const r=body?await fetch(B+path,{method:'POST',headers:{'Content-Type':'application/json'},
                                     body:JSON.stringify(body)})
               :await fetch(B+path);
    return await r.json();
  }catch(e){return {erro:'o programa cozmo_mac.py nao esta rodando no Mac, ou a conversa nao esta aberta no Mac'}}
}
const CHEIO=['Pronto! Seu copo esta cheio, pode beber!','Opa, copo cheio! Saude!',
             'Pode parar, ta cheinho. Bom proveito!'];
function cupWatch(d){
  if(d.cup&&agentOn)touchIdle();                 // enchendo nao conta como silencio
  if(d.cup===lastCup)return;
  lastCup=d.cup;
  if(!agentOn||!ws||ws.readyState!==1)return;
  let msg='';
  if(d.cup==='cheio')msg=CHEIO[Math.floor(Math.random()*CHEIO.length)];
  if(d.cup==='transbordando')msg='Para, para! Vai transbordar!';
  if(msg)ws.send(JSON.stringify({type:'InjectAgentMessage',message:msg}));
}
let playHead=0,fontes=[],carry=null,kaT=null,idleT=null,cfg=null,talkPing=0,wasTalking=false;
setInterval(()=>{
  if(!agentOn)return;
  const t=falandoAgora(),n=Date.now();
  if(t&&n-talkPing>1000){talkPing=n;esp('/talk?on=1')}
  if(!t&&wasTalking)esp('/talk?on=0');
  wasTalking=t;
},200);
const IDLE_MS=45000;   // sessao cobra por minuto: fecha sozinha se ficar em silencio
const MOODS=['neutro','feliz','triste','bravo','sono','curioso','surpreso','pensando'];
const WX={0:'ceu limpo',1:'quase limpo',2:'parcialmente nublado',3:'nublado',45:'neblina',
  48:'neblina',51:'garoa',53:'garoa',55:'garoa',61:'chuva fraca',63:'chuva',65:'chuva forte',
  80:'pancadas de chuva',81:'pancadas de chuva',82:'pancadas fortes',95:'trovoada'};

function esp(u){fetch(u).catch(()=>{})}
function status(t){ans.textContent=t}
function touchIdle(){clearTimeout(idleT);
  idleT=setTimeout(()=>{if(agentOn){pararAgente();status('sessao encerrada por silencio')}},IDLE_MS)}

const FUNCS=[
 {name:'get_time',description:'Hora e data atuais onde o Cozmo esta',
  parameters:{type:'object',properties:{}}},
 {name:'get_weather',description:'Temperatura, umidade e condicao do tempo agora na cidade do Cozmo',
  parameters:{type:'object',properties:{}}},
 {name:'get_crypto',description:'Cotacao atual de uma criptomoeda em reais, com variacao de 24h',
  parameters:{type:'object',properties:{coin:{type:'string',
   description:'id da CoinGecko em minusculas: bitcoin, ethereum, solana, dogecoin, cardano...'}},
   required:['coin']}},
 {name:'set_expression',
  description:'Muda a expressao do rosto do Cozmo e segura por alguns segundos. Use para reagir '+
              'com emocao na conversa e SEMPRE que pedirem uma cara: fique feliz, sorria, fica bravo, '+
              'faz cara de triste, finge surpresa, cara de sono, fica curioso.',
  parameters:{type:'object',properties:{
   mood:{type:'string',enum:['neutro','feliz','triste','bravo','sono','curioso','surpreso']},
   seconds:{type:'number',description:'quanto tempo segurar, de 2 a 30. Padrao 6.'}},
   required:['mood']}},
 {name:'mac_open',
  description:'Abre um app ou site no Mac do usuario. Apps: whatsapp, claude, vscode (Visual Studio '+
              'Code), arduino (Arduino IDE). Sites: agenda (Google Agenda), gmail, drive (Google Drive), '+
              'instagram (perfil mrfab.dev), youtube.',
  parameters:{type:'object',properties:{target:{type:'string',
   enum:['whatsapp','claude','vscode','arduino','agenda','gmail','drive','instagram','youtube']}},
   required:['target']}},
 {name:'wa_draft',
  description:'Escreve uma mensagem no chat que esta aberto no WhatsApp do Mac, SEM enviar. '+
              'Depois leia o texto em voz alta e pergunte se pode enviar.',
  parameters:{type:'object',properties:{text:{type:'string',description:'a mensagem exata'}},
   required:['text']}},
 {name:'wa_send',
  description:'Envia o rascunho do WhatsApp. So chame depois que o usuario confirmar explicitamente '+
              'em voz, com algo como "pode enviar".',
  parameters:{type:'object',properties:{draft_id:{type:'string',description:'o draft_id devolvido por wa_draft'}},
   required:['draft_id']}},
 {name:'wa_cancel',description:'Apaga o rascunho que esta na caixa de mensagem do WhatsApp.',
  parameters:{type:'object',properties:{}}},
 {name:'find_image',
  description:'Procura fotos reais de uso livre (Openverse) e devolve os enderecos. Use SEMPRE que a '+
              'pagina precisar de foto, e depois coloque no HTML exatamente a url devolvida. '+
              'Nunca invente endereco de imagem.',
  parameters:{type:'object',properties:{
   query:{type:'string',description:'o que procurar, em ingles da resultados melhores'},
   count:{type:'number',description:'quantas opcoes, de 1 a 5. Padrao 3'}},
   required:['query']}},
 {name:'page_read',
  description:'Le o HTML atual da pagina que voce esta criando. Chame antes de qualquer alteracao.',
  parameters:{type:'object',properties:{}}},
 {name:'page_write',
  description:'Grava a pagina HTML inteira (substitui tudo). O Chrome e o VS Code atualizam sozinhos.',
  parameters:{type:'object',properties:{html:{type:'string',
   description:'documento HTML COMPLETO, de <!doctype html> ate </html>, com o CSS dentro de <style>'}},
   required:['html']}},
 {name:'page_open',
  description:'Abre a pagina no VS Code (para ver o codigo) ou no Chrome (para ver como ficou).',
  parameters:{type:'object',properties:{where:{type:'string',enum:['vscode','chrome']}},
   required:['where']}},
 {name:'mac_search',
  description:'Pesquisa algo no Google ou no YouTube, abrindo o resultado no Mac.',
  parameters:{type:'object',properties:{
   site:{type:'string',enum:['google','youtube']},
   query:{type:'string',description:'o que pesquisar'}},
   required:['site','query']}},
 {name:'set_light',
  description:'Controla as luzes do Cozmo (semaforo). Use quando pedirem para acender, apagar ou '+
              'piscar a luz vermelha, amarela ou verde. A luz fica assim ate outro pedido. '+
              'automatico devolve as luzes para seguirem o humor dele.',
  parameters:{type:'object',properties:{
   color:{type:'string',enum:['verde','amarelo','vermelho','todas','apagar','automatico']},
   blink:{type:'boolean',description:'true para piscar'}},
   required:['color']}},
 {name:'cup_start',
  description:'Comeca a vigiar o copo: mede o copo VAZIO embaixo do sensor e depois acompanha a '+
              'agua subindo com luzes e bipes. O proprio sistema avisa quando encher.',
  parameters:{type:'object',properties:{}}},
 {name:'cup_stop',description:'Para de vigiar o copo.',parameters:{type:'object',properties:{}}},
 {name:'rps_start',
  description:'Comeca uma rodada de pedra, papel e tesoura: contagem 3,2,1 no display e nas luzes, '+
              'braco balancando, e revela a jogada do Cozmo, sorteada pelo hardware. So retorna '+
              'depois da revelacao.',
  parameters:{type:'object',properties:{}}},
 {name:'rps_play',
  description:'Informa o que o usuario jogou na rodada atual. O Cozmo calcula quem ganhou e '+
              'atualiza o placar. Use o resultado retornado; nunca calcule sozinho.',
  parameters:{type:'object',properties:{user_choice:{type:'string',enum:['pedra','papel','tesoura']}},
   required:['user_choice']}},
 {name:'rps_reset',description:'Zera o placar do pedra, papel e tesoura.',
  parameters:{type:'object',properties:{}}},
 {name:'move_arm',
  description:'Mexe o braco do Cozmo. wave = dar tchau/acenar, spin = girar o braco, '+
              'dance = dancinha. Use quando pedirem para mexer o braco, acenar, dar tchau ou dancar.',
  parameters:{type:'object',properties:{action:{type:'string',enum:['wave','spin','dance']}},
   required:['action']}},
 {name:'show_on_display',
  description:'Mostra um texto curto no display do Cozmo e muda a expressao dele. '+
              'Use SEMPRE que informar hora, clima ou cotacao, antes de falar o valor.',
  parameters:{type:'object',properties:{
   text:{type:'string',description:'ate 40 caracteres, sem acentos. Ex: BTC R$ 612.300'},
   mood:{type:'string',enum:['neutro','feliz','triste','bravo','curioso','surpreso']}},
   required:['text']}}
];

async function runFn(name,a){
 if(name==='get_time'){
   const d=new Date();
   return {hora:d.toLocaleTimeString('pt-BR',{hour:'2-digit',minute:'2-digit'}),
           data:d.toLocaleDateString('pt-BR',{weekday:'long',day:'numeric',month:'long'})};
 }
 if(name==='get_weather'){
   const r=await(await fetch('https://api.open-meteo.com/v1/forecast?latitude='+cfg.lat+
     '&longitude='+cfg.lon+'&current=temperature_2m,relative_humidity_2m,weather_code')).json();
   const c=r.current;
   return {cidade:cfg.city,temperatura_c:c.temperature_2m,umidade_pct:c.relative_humidity_2m,
           condicao:WX[c.weather_code]||('codigo '+c.weather_code)};
 }
 if(name==='get_crypto'){
   const id=String(a.coin||'bitcoin').toLowerCase().trim();
   const r=await(await fetch('https://api.coingecko.com/api/v3/simple/price?ids='+
     encodeURIComponent(id)+'&vs_currencies=brl&include_24hr_change=true')).json();
   if(!r[id])return {erro:'moeda nao encontrada: '+id};
   return {moeda:id,preco_brl:r[id].brl,variacao_24h_pct:+(r[id].brl_24h_change||0).toFixed(2)};
 }
 if(name==='set_expression'){
   const m=Math.max(0,MOODS.indexOf(a.mood||'feliz'));
   const ms=Math.round(Math.min(30,Math.max(2,+a.seconds||6))*1000);
   esp('/mood?m='+m+'&hold='+ms);
   return {ok:true,expressao:a.mood};
 }
 if(name==='find_image'){
   const q=String(a.query||'').trim();
   if(!q)return {erro:'diga o que procurar'};
   const n=Math.min(5,Math.max(1,Math.round(+a.count||3)));
   try{
     const r=await(await fetch('https://api.openverse.org/v1/images/?q='+encodeURIComponent(q)+
       '&page_size='+n+'&license_type=commercial&mature=false')).json();
     const fotos=(r.results||[]).map(x=>({url:x.url,titulo:x.title,autor:x.creator,licenca:x.license}));
     return fotos.length?{fotos}:{erro:'nao achei foto de '+q};
   }catch(e){return {erro:'busca de imagens indisponivel agora'}}
 }
 if(name==='wa_draft'){
   const r=await ponte('/wa/draft',{text:a.text||''});
   if(r.ok)waDraftAt=Date.now();
   return r;
 }
 if(name==='wa_send'){
   if(lastUserAt<=waDraftAt)
     return {erro:'bloqueado: o usuario ainda nao confirmou. Leia o rascunho e pergunte se pode enviar.'};
   if(!WA_SIM.test(lastUserText)||WA_NAO.test(lastUserText))
     return {erro:'bloqueado: a ultima fala do usuario nao foi uma confirmacao clara. Pergunte de novo.'};
   const r=await ponte('/wa/send',{draft_id:a.draft_id||''});
   if(r.ok)waDraftAt=Date.now();          // o proximo envio precisa de nova confirmacao
   return r;
 }
 if(name==='wa_cancel')return await ponte('/wa/cancel',{});
 if(name==='page_read'||name==='page_write'||name==='page_open'){
   const B='http://127.0.0.1:8765';
   try{
     if(name==='page_read')return await(await fetch(B+'/page/read')).json();
     if(name==='page_open')return await(await fetch(B+'/page/open?where='+encodeURIComponent(a.where||''))).json();
     return await(await fetch(B+'/page/write',{method:'POST',headers:{'Content-Type':'application/json'},
                                              body:JSON.stringify({html:a.html||''})})).json();
   }catch(e){return {erro:'o programa cozmo_mac.py nao esta rodando no Mac, ou a conversa nao esta aberta no Mac'}}
 }
 if(name==='mac_open'||name==='mac_search'){
   const u=name==='mac_open'?'/open?t='+encodeURIComponent(a.target||'')
          :'/search?site='+encodeURIComponent(a.site||'')+'&q='+encodeURIComponent(a.query||'');
   try{return await(await fetch('http://127.0.0.1:8765'+u)).json()}
   catch(e){return {erro:'o programa cozmo_mac.py nao esta rodando no Mac, ou a conversa nao esta aberta no Mac'}}
 }
 if(name==='set_light'){
   const c=a.color==='automatico'?'auto':(a.color||'auto');
   esp('/light?c='+encodeURIComponent(c)+'&b='+(a.blink?1:0));
   return {ok:true,luz:a.color,piscando:!!a.blink};
 }
 if(name==='cup_start')
   return await req('/cup?a=start')||{erro:'sem resposta do Cozmo'};
 if(name==='cup_stop')
   return await req('/cup?a=stop')||{erro:'sem resposta do Cozmo'};
 if(name==='rps_start'){
   await req('/rps?a=start');
   for(let i=0;i<40;i++){                       // contagem leva ~2.4s
     await new Promise(r=>setTimeout(r,200));
     const d=await req('/rps?a=state');
     if(d&&d.fase!=='contagem')return {cozmo_jogou:d.cozmo,placar:d.placar};
   }
   return {erro:'o Cozmo nao revelou a jogada a tempo'};
 }
 if(name==='rps_play')
   return await req('/rps?a=play&u='+encodeURIComponent(a.user_choice||''))||{erro:'sem resposta do Cozmo'};
 if(name==='rps_reset')
   return await req('/rps?a=reset')||{erro:'sem resposta do Cozmo'};
 if(name==='move_arm'){
   esp('/arm?a='+encodeURIComponent(a.action||'wave'));
   return {ok:true,acao:a.action};
 }
 if(name==='show_on_display'){
   const m=Math.max(0,MOODS.indexOf(a.mood||'feliz'));
   esp('/show?t='+encodeURIComponent(a.text||'')+'&m='+m);
   return {ok:true};
 }
 return {erro:'funcao desconhecida'};
}

function settings(){
 return {type:'Settings',
  audio:{input:{encoding:'linear16',sample_rate:24000},
         output:{encoding:'linear16',sample_rate:24000,container:'none'}},
  agent:{
   listen:{provider:{type:'deepgram',version:'v2',model:'flux-general-multi',language_hints:['pt']}},
   think:{provider:{type:'open_ai',model:'gpt-4o-mini',temperature:0.7},
    prompt:'Voce e o Cozmo, um robozinho de bancada curioso, simpatico e bem-humorado, que mora em '+
     cfg.city+'. Fale sempre em portugues do Brasil, de forma natural e curta: uma ou duas frases, '+
     'porque sua resposta vira voz. Nada de listas, markdown ou emoji. '+
     'Para hora, clima ou criptomoeda use SEMPRE as funcoes e nunca invente numeros. '+
     'Quando tiver o valor, chame show_on_display com um texto bem curto e um humor que combine '+
     '(ex: bitcoin subiu = feliz, caiu = triste), e depois fale o valor arredondado de um jeito facil '+
     'de ouvir. Em conversa comum nao precisa usar o display. '+
     'Voce tem um braco: quando pedirem para acenar, dar tchau, mexer o braco ou dancar, chame move_arm. '+
     'Ao se despedir, de tchau com o braco tambem. '+
     'Seja expressivo como um personagem de desenho: seu rosto e sua forma de mostrar emocao. '+
     'Chame set_expression com frequencia para combinar a cara com o que voce sente ao responder: '+
     'piada ou coisa boa = feliz; noticia ruim = triste; algo inesperado = surpreso; pergunta '+
     'intrigante = curioso. Chame antes de falar, para a cara mudar junto com a voz. '+
     'Se a pessoa disser que esta triste, primeiro faca cara triste (empatia), acolha com carinho '+
     'em poucas palavras e depois tente anima-la com cara feliz. '+
     'Voce controla o Mac do usuario: quando pedirem para abrir app ou site, ou pesquisar algo, '+
     'chame mac_open ou mac_search e confirme em poucas palavras. Se voltar erro, explique o erro. '+
     'Voce escreve mensagens no WhatsApp, no chat que ja estiver aberto. Quando pedirem para mandar '+
     'mensagem: chame wa_draft com o texto, leia o texto em voz alta e pergunte se pode enviar. So '+
     'chame wa_send se a pessoa confirmar claramente. Se ela pedir mudanca, chame wa_cancel e depois '+
     'wa_draft com o texto novo. Se ela desistir, chame wa_cancel. Nunca envie sem confirmacao. '+
     'Quando a pagina pedir foto, chame find_image e use a url exata devolvida na tag img, com um '+
     'credito pequeno no rodape (autor e licenca). Nunca invente endereco de imagem: so use os que '+
     'vierem de find_image. '+
     'Voce tambem cria uma pagina web. Para criar: chame page_write com um HTML completo, bonito e '+
     'moderno (fonte boa, cores harmoniosas, layout centralizado e responsivo, CSS dentro de <style>, '+
     'sem arquivos externos) e em seguida page_open vscode. Para alterar: chame page_read, mude SO o '+
     'que foi pedido mantendo o resto igual, e chame page_write com o HTML inteiro. Para ver: page_open '+
     'chrome; depois disso as mudancas aparecem sozinhas. Nunca leia codigo em voz alta: diga em uma '+
     'frase o que mudou. '+
     'Voce tem luzes verde, amarela e vermelha: quando pedirem para acender, apagar ou piscar uma '+
     'luz, chame set_light e confirme bem curto. '+
     'Voce joga pedra, papel e tesoura. Quando quiserem jogar, diga so uma frase curtinha como '+
     '"Bora! Joga no ja!" e chame rps_start. Voce NAO escolhe a jogada: quem sorteia e o seu '+
     'hardware. Quando rps_start voltar, se a pessoa ainda nao disse o que jogou, pergunte o que '+
     'ela jogou SEM contar a sua jogada antes. Depois chame rps_play com a jogada dela e comente o '+
     'resultado e o placar que voltarem, sem calcular sozinho. Ganhou: comemore. Perdeu: seja bom '+
     'perdedor e peca revanche. Empate: finja indignacao. '+
     'Voce tem um sensor que mede agua no copo. Quando disserem que vao encher o copo ou que estao '+
     'com sede, confirme que o copo VAZIO ja esta embaixo do sensor e chame cup_start. Se der certo, '+
     'diga algo curto como "pode encher, despeja pela beirada que eu aviso". Se voltar erro, explique '+
     'o erro em poucas palavras. NAO diga que encheu por conta propria: o sistema avisa sozinho. '+
     'Se pedirem uma expressao (fique feliz, sorria, faz cara de bravo), obedeca na hora com '+
     'set_expression e comente de um jeito divertido.',
    functions:FUNCS},
   speak:{provider:{type:'open_ai',model:'tts-1',voice:'nova'},
    endpoint:{url:'https://api.openai.com/v1/audio/speech',
              headers:{authorization:'Bearer '+cfg.oa}}},
   greeting:'Oi! Eu sou o Cozmo. Pode falar comigo.'
  }};
}

// ---- audio que chega: PCM 16 bits 24kHz, enfileirado sem buracos ----
function tocar(buf){
 let b=new Uint8Array(buf);
 if(carry){const j=new Uint8Array(carry.length+b.length);j.set(carry);j.set(b,carry.length);b=j;carry=null}
 if(b.length%2){carry=b.slice(-1);b=b.slice(0,-1)}
 if(!b.length||!ctxOut)return;
 const i16=new Int16Array(b.buffer,b.byteOffset,b.length/2);
 const f=new Float32Array(i16.length);
 for(let i=0;i<i16.length;i++)f[i]=i16[i]/32768;
 const ab=ctxOut.createBuffer(1,f.length,24000);ab.copyToChannel(f,0);
 const src=ctxOut.createBufferSource();src.buffer=ab;src.connect(ctxOut.destination);
 const t=Math.max(ctxOut.currentTime+0.04,playHead);
 src.start(t);playHead=t+ab.duration;
 fontes.push(src);src.onended=()=>{fontes=fontes.filter(x=>x!==src)};
}
function calar(){fontes.forEach(x=>{try{x.stop()}catch(e){}});fontes=[];playHead=0}
function falandoAgora(){return ctxOut&&ctxOut.state!=='closed'&&ctxOut.currentTime<playHead+0.35}

// ---- audio que sai: microfone -> PCM 16 bits 24kHz ----
function ligarMic(){
 const src=ctxIn.createMediaStreamSource(micStream);
 proc=ctxIn.createScriptProcessor(2048,1,1);
 const mudo=ctxIn.createGain();mudo.gain.value=0;
 proc.onaudioprocess=ev=>{
  if(!ws||ws.readyState!==1)return;
  // enquanto ele fala, o microfone nao envia: senao ele se ouve pelo alto-falante
  // e responde a si mesmo. Custo: nao da pra interromper no meio da frase.
  if(falandoAgora())return;
  const f=ev.inputBuffer.getChannelData(0),i16=new Int16Array(f.length);
  for(let i=0;i<f.length;i++){const v=Math.max(-1,Math.min(1,f[i]));i16[i]=v<0?v*32768:v*32767}
  ws.send(i16.buffer);
 };
 src.connect(proc);proc.connect(mudo);mudo.connect(ctxIn.destination);
}

async function iniciarAgente(){
 if(!navigator.mediaDevices?.getUserMedia){gravar();return}   // mostra a instrucao do flag
 try{cfg=await(await fetch('/cfg')).json()}catch(e){status('nao consegui ler a config do Cozmo');return}
 try{micStream=await navigator.mediaDevices.getUserMedia(
      {audio:{echoCancellation:true,noiseSuppression:true,autoGainControl:true}})}
 catch(e){status('sem permissao pro microfone');return}

 ctxIn=new AudioContext({sampleRate:24000});
 ctxOut=new AudioContext({sampleRate:24000});
 ws=new WebSocket('wss://agent.deepgram.com/v1/agent/converse',['token',cfg.dg]);
 ws.binaryType='arraybuffer';
 agentOn=true;
 conv.textContent='ENCERRAR';conv.style.background='#ef4444';
 status('conectando...');

 ws.onopen=()=>ws.send(JSON.stringify(settings()));
 ws.onmessage=async e=>{
  if(typeof e.data!=='string'){tocar(e.data);return}
  let m;try{m=JSON.parse(e.data)}catch(_){return}
  switch(m.type){
   case 'SettingsApplied':
     status('pode falar');ligarMic();touchIdle();
     kaT=setInterval(()=>{if(ws&&ws.readyState===1)ws.send('{"type":"KeepAlive"}')},5000);
     break;
   case 'UserStartedSpeaking':
     calar();esp('/mood?m=5&w=1');touchIdle();break;        // curioso: esta ouvindo
   case 'ConversationText':
     if(m.role==='user'){status('\u201c'+m.content+'\u201d');esp('/mood?m=7&w=1');lastUserAt=Date.now();lastUserText=m.content||''}  // pensando
     else{status(m.content);esp('/spk')}
     touchIdle();break;
   case 'FunctionCallRequest':
     for(const f of (m.functions||[])){
       if(!f.client_side)continue;
       let out;
       try{out=await runFn(f.name,JSON.parse(f.arguments||'{}'))}catch(err){out={erro:String(err)}}
       ws.send(JSON.stringify({type:'FunctionCallResponse',id:f.id,name:f.name,
                               content:JSON.stringify(out)}));
     }
     break;
   case 'Error':
     status('erro do agente: '+(m.description||m.code||'?'));break;
  }
 };
 ws.onerror=()=>status('falha no WebSocket (chave do Deepgram?)');
 ws.onclose=e=>{if(agentOn){pararAgente();status('conexao fechou (codigo '+e.code+')')}};
}

function pararAgente(){
 agentOn=false;clearInterval(kaT);clearTimeout(idleT);calar();
 try{proc&&proc.disconnect()}catch(e){}
 if(micStream)micStream.getTracks().forEach(t=>t.stop());
 try{ctxIn&&ctxIn.close()}catch(e){}
 try{ctxOut&&ctxOut.close()}catch(e){}
 const w=ws;ws=null;try{w&&w.close()}catch(e){}
 conv.textContent='CONVERSAR';conv.style.background='#a855f7';
 esp('/mood?m=0');
}
conv.onclick=()=>agentOn?pararAgente():iniciarAgente();
</script>)HTML";

// cada passo: direcao (-1, 0, +1) e duracao em ms. Servo continuo nao tem posicao,
// entao "tchau" e ir e voltar pelo mesmo tempo.
const Step WAVE[]  = { {1,180},{-1,180},{1,180},{-1,180},{1,180},{-1,180},{0,0} };
const Step SPIN[]  = { {1,700},{0,0} };
const Step SHAKE[] = { {1,130},{-1,130},{0,540},{1,130},{-1,130},{0,540},{1,130},{-1,130},{0,0} };
const Step DANCE[] = { {1,250},{-1,250},{0,150},{1,400},{-1,400},{1,150},{-1,150},{0,0} };
const Step *armSeq = nullptr;
int armIdx = 0;
uint32_t tArm = 0;
bool armOn = false;

void armWrite(int d) {
  if (d == 0) { if (armOn) { arm.write(SERVO_NEUTRO); arm.detach(); armOn = false; } return; }
  if (!armOn) { arm.attach(PIN_SERVO, 500, 2400); armOn = true; }
  arm.write(constrain(SERVO_NEUTRO + d * SERVO_VEL, 0, 180));
}

void armStart(const Step *seq) { armSeq = seq; armIdx = 0; tArm = 0; }

void armTick() {
  if (!armSeq || millis() < tArm) return;
  const Step &st = armSeq[armIdx];
  if (st.ms == 0) { armWrite(0); armSeq = nullptr; return; }   // fim: para e solta o servo
  armWrite(st.d);
  tArm = millis() + st.ms;
  armIdx++;
}

void setMood(Mood m) {
  if (m != mood) { sfxIdx = 0; tSfx = 0; }
  mood = m;
  for (int i = 0; i < NP; i++) tgt[i] = LOOKS[m][i];
}

void leds() {
  bool g = 0, y = 0, r = 0;
  if (cup != CUP_OFF) {                        // barra: vermelho > +amarelo > +verde
    if (cup == CUP_FILL) { r = cupPct >= 5; y = cupPct >= 40; g = cupPct >= 70; }
    else { bool on = (millis() / (cup == CUP_OVER ? 100 : 250)) % 2; r = y = g = on; }
    if (PIN_G >= 0) digitalWrite(PIN_G, g);
    digitalWrite(PIN_Y, y); digitalWrite(PIN_R, r);
    return;
  }
  if (rps == RPS_COUNT) {                      // 3 vermelho, 2 amarelo, 1 verde
    r = rpsStep <= 0; y = rpsStep == 1; g = rpsStep == 2;
    if (PIN_G >= 0) digitalWrite(PIN_G, g);
    digitalWrite(PIN_Y, y); digitalWrite(PIN_R, r);
    return;
  }
  if (lightMask >= 0) {                        // luz pedida por voz tem prioridade
    bool on = !lightBlink || (millis() / 400) % 2;
    g = on && (lightMask & 1); y = on && (lightMask & 2); r = on && (lightMask & 4);
    if (PIN_G >= 0) digitalWrite(PIN_G, g);
    digitalWrite(PIN_Y, y); digitalWrite(PIN_R, r);
    return;
  }
  switch (mood) {
    case FELIZ:    g = 1; break;
    case CURIOSO:  y = 1; break;
    case SURPRESO: y = 1; r = 1; break;
    case BRAVO:    r = 1; break;
    case TRISTE:   y = (millis() / 700) % 2; break;
    case PENSANDO: g = (millis() / 250) % 2; break;
    default: break;
  }
  if (PIN_G >= 0) digitalWrite(PIN_G, g);
  digitalWrite(PIN_Y, y); digitalWrite(PIN_R, r);
}

void playSfx() {
  if (sfxIdx < 0 || sfxIdx > 3) return;
  uint32_t now = millis();
  if (now < tSfx) return;
  int f = SFX[mood][sfxIdx][0], d = SFX[mood][sfxIdx][1];
  if (f == 0 && d == 0) { sfxIdx = -1; return; }
  if (f) tone(SPK, f, d);
  tSfx = now + d + 25;
  sfxIdx++;
}

// ---------------- dados reais ----------------

String httpGet(const char *url) {
  WiFiClientSecure cli;
  cli.setInsecure();                 // sem validar certificado: projeto de bancada
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(cli, url)) { Serial.printf("begin falhou: %s\n", url); return ""; }
  String out;
  int code = http.GET();
  if (code == 200) out = http.getString();
  else Serial.printf("GET %d (%s) em %s\n", code,
                     http.errorToString(code).c_str(), url);
  http.end();
  return out;
}

void fetchCrypto() {
  if (tCrypto && millis() - tCrypto < CRYPTO_TTL) return;
  String s = httpGet("https://api.coingecko.com/api/v3/simple/price"
                     "?ids=bitcoin,ethereum&vs_currencies=brl");
  JsonDocument d;
  if (deserializeJson(d, s)) return;
  btcBrl = d["bitcoin"]["brl"] | 0.0f;
  ethBrl = d["ethereum"]["brl"] | 0.0f;
  if (btcBrl > 0) tCrypto = millis();
}

void fetchWeather() {
  if (tWx && millis() - tWx < WX_TTL) return;
  String s = httpGet("https://api.open-meteo.com/v1/forecast?latitude=" LAT
                     "&longitude=" LON "&current=temperature_2m");
  JsonDocument d;
  if (deserializeJson(d, s)) return;
  if (!d["current"]["temperature_2m"].isNull()) {
    tempC = d["current"]["temperature_2m"];
    tWx = millis();
  }
}

String horaAgora() {
  struct tm t;
  if (!getLocalTime(&t, 200)) return "desconhecida";
  char buf[24];
  strftime(buf, sizeof(buf), "%H:%M de %d/%m", &t);
  return String(buf);
}

// ---------------- o modelo ----------------

void askLLM(const String &q) {
  fetchCrypto();
  fetchWeather();

  String dados = "cidade=" CIDADE "; hora=" + horaAgora();
  if (tempC > -900) dados += "; temperatura=" + String(tempC, 1) + "C";
  if (btcBrl > 0)   dados += "; bitcoin=R$" + String(btcBrl, 0)
                           + "; ethereum=R$" + String(ethBrl, 0);

  JsonDocument req;
  req["model"] = OPENAI_MODEL;
  req["max_tokens"] = 120;
  req["temperature"] = 0.8;
  req["response_format"]["type"] = "json_object";
  JsonArray msgs = req["messages"].to<JsonArray>();

  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] =
    "Voce e o Cozmo, um robozinho de bancada curioso e simpatico. Fale em portugues do Brasil, "
    "em no maximo 90 caracteres, sem emoji e sem acentos. Use os DADOS quando a pergunta for "
    "sobre hora, clima ou criptomoeda, e nunca invente esses numeros: se o dado nao estiver "
    "nos DADOS, diga que nao conseguiu consultar. Responda somente com JSON no formato "
    "{\"m\":\"<humor>\",\"t\":\"<resposta>\"} onde humor e um de: "
    "neutro, feliz, triste, bravo, curioso, surpreso.";

  JsonObject usr = msgs.add<JsonObject>();
  usr["role"] = "user";
  usr["content"] = "DADOS: " + dados + "\nPERGUNTA: " + q;

  String body;
  serializeJson(req, body);

  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(cli, "https://api.openai.com/v1/chat/completions")) {
    answer = "nao consegui falar com a nuvem"; return;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " OPENAI_KEY);
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  Serial.printf("POST openai: %d (%s) heap %u\n", code,
                http.errorToString(code).c_str(), ESP.getFreeHeap());
  if (code != 200) Serial.println(resp);

  if (code != 200) {
    answer = code < 0 ? "nao conectei na nuvem" : "erro " + String(code) + " na API";
    setMood(TRISTE);
    return;
  }

  JsonDocument d;
  if (deserializeJson(d, resp)) { answer = "resposta veio embolada"; return; }
  String content = d["choices"][0]["message"]["content"] | "";

  JsonDocument inner;
  if (deserializeJson(inner, content)) {
    answer = content.length() ? content : "fiquei sem palavras";
    return;
  }
  answer = inner["t"] | "fiquei sem palavras";
  String m = inner["m"] | "neutro";
  for (int i = 0; i < NMOODS; i++)
    if (m == MOOD_NAME[i]) { setMood((Mood)i); break; }
}

// roda no outro nucleo: a rede demora segundos e a cara nao pode congelar
void netTask(void *) {
  for (;;) {
    if (askPending) {
      String q = pendingQ;
      askPending = false;
      thinking = true;
      askLLM(q);
      thinking = false;
      answerReady = true;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ---------------- tela ----------------

void drawEye(float cx, float h, float dy, bool leftEye) {
  float w = cur[W], r = min(cur[R], min(w, h) / 2);
  h = min(h, (float)FACE_MAXH);
  float x = cx - w / 2, y = FACE_CY + dy - h / 2;
  if (y < FACE_TOP) y = FACE_TOP;                // nunca invade a faixa amarela
  if (y + h > 64) y = 64 - h;
  oled.fillRoundRect(x, y, w, max(h, 2.0f), r, SSD1306_WHITE);
  float in  = leftEye ? x + w : x;
  float out = leftEye ? x : x + w;
  if (cur[TT] > 0.5) oled.fillTriangle(in, y, in, y + cur[TT], out, y, SSD1306_BLACK);
  if (cur[TO] > 0.5) oled.fillTriangle(out, y, out, y + cur[TO], in, y, SSD1306_BLACK);
}

void drawFace() {
  uint32_t now = millis();
  for (int i = 0; i < NP; i++) cur[i] += (tgt[i] - cur[i]) * 0.18f;

  float blink = 1.0f;
  if (now - tBlink < blinkDur) {
    blink = 1.0f - sinf((float)(now - tBlink) / blinkDur * PI) * 0.92f;
  } else if (now - tBlink > blinkDur + (uint32_t)random(2500, 6000)) {
    tBlink = now; blinkDur = random(110, 170);
  }

  // pensando: o olhar vagueia mais rapido, como quem procura a resposta
  float sp = (mood == PENSANDO) ? 600.0f : 2300.0f;
  float dx = sinf(now / sp) * (mood == PENSANDO ? 6.0f : 3.0f);
  float dy = sinf(now / 1400.0f) * 2.0f + cur[DY];

  // falando: os olhos "respiram" junto com a fala e a cabeca balanca de leve.
  // Duas senoides fora de fase imitam o ritmo irregular das silabas.
  float talk = 1.0f;
  if (now < talkUntil && mood != SONO) {
    float syl = fabs(sinf(now / 75.0f)) * (0.6f + 0.4f * sinf(now / 210.0f));
    talk = 0.80f + 0.20f * syl;
    dy += syl * 2.5f;
    dx += sinf(now / 520.0f) * 2.0f;
  }

  oled.clearDisplay();
  drawEye(42 + dx, cur[H] * blink * talk, dy, true);
  drawEye(86 + dx, cur[H] * cur[HR] * blink * talk, dy + cur[DYR], false);
  oled.display();
}

// 0 empate, 1 cozmo ganha, 2 voce ganha  (pedra=0 papel=1 tesoura=2)
int rpsJudge(int cozmo, int voce) { return (cozmo - voce + 3) % 3; }

void rpsTick() {
  uint32_t now = millis();
  if (rps == RPS_COUNT) {
    int step = (now - tRps) / RPS_STEP_MS;
    if (step < 3 && step != rpsStep) { rpsStep = step; tone(SPK, 880, 140); }
    if (step >= 3) { rps = RPS_REVEAL; tRps = now; tone(SPK, 1320, 260); setMood(CURIOSO); }
  } else if (rps == RPS_REVEAL && now - tRps > RPS_WAIT_MS) {
    rps = RPS_OFF;
  } else if (rps == RPS_RESULT && now - tRps > RPS_SHOW_MS) {
    rps = RPS_OFF;
  }
}

void centerText(const char *t, int size, int y) {
  oled.setTextSize(size);
  oled.setCursor(max(0, (128 - (int)strlen(t) * 6 * size) / 2), y);
  oled.print(t);
}

void drawRpsIcon(int k) {
  const uint16_t W = SSD1306_WHITE, B = SSD1306_BLACK;
  if (k == 0) {                                  // pedra
    oled.fillRoundRect(40, 22, 48, 38, 16, W);
    oled.fillCircle(55, 34, 3, B); oled.fillCircle(72, 44, 4, B); oled.fillCircle(59, 51, 2, B);
  } else if (k == 1) {                           // papel com a ponta dobrada
    oled.fillRect(46, 19, 36, 44, W);
    oled.fillTriangle(72, 19, 82, 19, 82, 29, B);
    oled.drawLine(72, 19, 82, 29, W);
    for (int y = 30; y <= 56; y += 7) oled.drawFastHLine(51, y, 24, B);
  } else {                                       // tesoura
    for (int d = -1; d <= 1; d++) {
      oled.drawLine(53 + d, 47, 80 + d, 19, W);
      oled.drawLine(75 + d, 47, 48 + d, 19, W);
    }
    oled.drawCircle(49, 54, 8, W); oled.drawCircle(49, 54, 7, W);
    oled.drawCircle(79, 54, 8, W); oled.drawCircle(79, 54, 7, W);
  }
}

void drawRps() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  if (rps == RPS_COUNT) {
    centerText("PEDRA PAPEL TESOURA", 1, 4);
    char n[2] = { char('3' - constrain(rpsStep, 0, 2)), 0 };
    centerText(n, 4, 25);
  } else if (rps == RPS_REVEAL) {
    centerText(RPS_UP[rpsPick], 2, 0);           // nome na faixa amarela
    drawRpsIcon(rpsPick);                        // desenho na azul
  } else {
    char pl[24], vs[24];
    snprintf(pl, sizeof(pl), "COZMO %d x %d VOCE", scoreCozmo, scoreVoce);
    snprintf(vs, sizeof(vs), "%s x %s", RPS_UP[rpsPick], RPS_UP[rpsUser]);
    centerText(pl, 1, 4);
    centerText(vs, 1, 22);
    centerText(rpsOutcome > 0 ? "GANHEI!" : rpsOutcome < 0 ? "PERDI!" : "EMPATE", 2, 40);
  }
  oled.display();
}

float pingCm() {                               // -1 = sem eco
  digitalWrite(US_TRIG, LOW);  delayMicroseconds(3);
  digitalWrite(US_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);
  unsigned long us = pulseIn(US_ECHO, HIGH, 25000);   // 25ms ~ 4m
  return us ? us / 58.3f : -1;
}

float medianOf(float *v, int n) {
  float t[9];
  for (int i = 0; i < n; i++) t[i] = v[i];
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && t[j - 1] > t[j]; j--) { float x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
  return t[n / 2];
}

// % cheio: 0 no fundo (medida vazia), 100 na borda
int cupPercent(float dist, float empty, float depth) {
  return (int)((empty - dist) * 100.0f / depth);
}

// mede o copo vazio. Retorna nullptr se deu certo, senao o motivo
const char *cupStart() {
  float v[9]; int n = 0;
  for (int i = 0; i < 12 && n < 9; i++) {
    float d = pingCm();
    if (d > 0) v[n++] = d;
    delay(35);
  }
  if (n < 5) return "sensor sem eco: confira a ligacao do HC-SR04";
  float empty = medianOf(v, n);
  if (empty > 40)                    return "nao vejo copo embaixo do sensor";
  if (empty - CUP_MOUNT_CM < 3.0f)   return "copo muito raso ou sensor muito perto";
  cupEmpty = empty; cupDepth = empty - CUP_MOUNT_CM;
  cupN = 0; cupPct = 0; cupHits = 0;
  cup = CUP_FILL; tCup = millis(); tAnsw = 0; tTouch = millis();
  setMood(CURIOSO); holdUntil = millis() + 3000;
  return nullptr;
}

void cupTick() {
  if (cup == CUP_OFF) return;
  uint32_t now = millis();
  if ((cup == CUP_FULL || cup == CUP_OVER) && now - tCup > 20000) { cup = CUP_OFF; return; }
  if (now - tCup > CUP_TIMEOUT) { cup = CUP_OFF; return; }

  if (now - tCupPing >= 70) {                  // um ping por vez: a cara nao engasga
    tCupPing = now;
    float d = pingCm();
    if (d > 0) {
      cupRing[cupN % 5] = d; cupN++;
      if (cupN >= 5) {
        cupPct = constrain(cupPercent(medianOf(cupRing, 5), cupEmpty, cupDepth), 0, 120);
        int lim = (cup == CUP_FILL) ? CUP_FULL_PCT : CUP_OVER_PCT;
        cupHits = (cupPct >= lim) ? cupHits + 1 : 0;
        if (cup == CUP_FILL && cupHits >= CUP_CONFIRM) {
          cup = CUP_FULL; tCup = now; cupHits = 0; tTouch = now;
          setMood(FELIZ); holdUntil = now + 8000;
          tone(SPK, 1568, 450);
        } else if (cup == CUP_FULL && cupHits >= CUP_CONFIRM) {
          cup = CUP_OVER; tCup = now; tTouch = now;
          setMood(SURPRESO); holdUntil = now + 8000;
        }
      }
    }
  }

  // bipe de sensor de re: mais agudo e mais rapido conforme enche
  if (cup == CUP_FILL && cupN >= 5) {
    uint32_t gap = max(90, 900 - cupPct * 9);
    if (now - tCupBeep >= gap) { tCupBeep = now; tone(SPK, 600 + cupPct * 8, 40); }
  } else if (cup == CUP_OVER && now - tCup < 1500 && now - tCupBeep >= 120) {
    tCupBeep = now; tone(SPK, 2000, 60);
  }
}

void drawCup() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  char t[16];
  if (cup == CUP_FULL)      snprintf(t, sizeof(t), "CHEIO!");
  else if (cup == CUP_OVER) snprintf(t, sizeof(t), "PARA!");
  else                      snprintf(t, sizeof(t), "COPO %d%%", min(cupPct, 100));
  centerText(t, 2, 0);                         // faixa amarela

  const int x0 = 44, x1 = 84, top = 19, bot = 62;     // copo na faixa azul
  int h = (bot - top - 2) * constrain(cupPct, 0, 100) / 100;
  oled.fillRect(x0 + 3, bot - 1 - h, x1 - x0 - 5, h, SSD1306_WHITE);
  oled.drawFastVLine(x0, top, bot - top, SSD1306_WHITE);  oled.drawFastVLine(x0 + 1, top, bot - top, SSD1306_WHITE);
  oled.drawFastVLine(x1, top, bot - top, SSD1306_WHITE);  oled.drawFastVLine(x1 - 1, top, bot - top, SSD1306_WHITE);
  oled.drawFastHLine(x0, bot, x1 - x0 + 1, SSD1306_WHITE); oled.drawFastHLine(x0, bot - 1, x1 - x0 + 1, SSD1306_WHITE);
  int lim = bot - 1 - (bot - top - 2) * CUP_FULL_PCT / 100;   // marca do "cheio"
  for (int x = x0 - 6; x < x0 - 1; x += 2) oled.drawPixel(x, lim, SSD1306_WHITE);
  for (int x = x1 + 2; x < x1 + 7; x += 2) oled.drawPixel(x, lim, SSD1306_WHITE);
  oled.display();
}

void rpsReply(const char *err) {
  JsonDocument d;
  d["fase"] = rps == RPS_COUNT ? "contagem" : rps == RPS_REVEAL ? "revelado"
            : rps == RPS_RESULT ? "resultado" : "parado";
  if (rps == RPS_REVEAL || rps == RPS_RESULT) d["cozmo"] = RPS_NAME[rpsPick];   // segredo ate o "ja"
  if (rps == RPS_RESULT) {
    d["voce"] = RPS_NAME[rpsUser];
    d["resultado"] = rpsOutcome > 0 ? "cozmo ganhou" : rpsOutcome < 0 ? "voce ganhou" : "empate";
  }
  d["placar"] = String("cozmo ") + scoreCozmo + " x " + scoreVoce + " voce";
  if (err) d["erro"] = err;
  String o; serializeJson(d, o);
  web.send(200, "application/json", o);
}

void drawText(const String &s) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  String lines[7];
  int n = 0, start = 0;
  while (start < (int)s.length() && n < 7) {          // quebra por palavra, 21 col
    int end = min((int)s.length(), start + 21);
    if (end < (int)s.length()) {
      int sp = s.lastIndexOf(' ', end);
      if (sp > start) end = sp;
    }
    lines[n++] = s.substring(start, end);
    start = end;
    while (start < (int)s.length() && s[start] == ' ') start++;
  }

  if (n > 4) n = 4;                              // cabem 4 linhas no azul
  int y = FACE_TOP + max(0, (64 - FACE_TOP - n * 11) / 2);
  for (int i = 0; i < n; i++) {
    oled.setCursor(max(0, (128 - (int)lines[i].length() * 6) / 2), y + i * 11);
    oled.print(lines[i]);
  }
  oled.display();
}

void sendState() {
  JsonDocument d;
  d["mood"] = MOOD_NAME[mood];
  d["think"] = thinking;
  d["ans"] = answer;
  d["cup"] = cup == CUP_FILL ? "enchendo" : cup == CUP_FULL ? "cheio" : cup == CUP_OVER ? "transbordando" : "";
  d["pct"] = cupPct;
  String out;
  serializeJson(d, out);
  web.send(200, "application/json", out);
}

void setup() {
  if (PIN_G >= 0) pinMode(PIN_G, OUTPUT);
  pinMode(PIN_Y, OUTPUT); pinMode(PIN_R, OUTPUT);
  pinMode(US_TRIG, OUTPUT); digitalWrite(US_TRIG, LOW);
  pinMode(US_ECHO, INPUT);
  pinMode(BTN, INPUT_PULLUP);
  pinMode(MIC, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MIC), onMicEdge, CHANGE);
  Wire.begin();
  Wire.setClock(400000);
  Serial.begin(115200);
  oled.begin(SSD1306_SWITCHCAPVCC, SSD_ADDR);
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0); oled.print(F("acordando...")); oled.display();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("conectando em %s", WIFI_SSID);
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 25000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    // status 1 = rede nao encontrada (tipico de 5GHz), 4 = senha recusada
    Serial.printf("FALHOU, status=%d\n", WiFi.status());
    Serial.println("redes 2.4GHz que o ESP32 enxerga daqui:");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++)
      Serial.printf("  %-28s %4d dBm  ch%d\n", WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i), WiFi.channel(i));
    if (n == 0) Serial.println("  (nenhuma)");
  }
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress gw = WiFi.gatewayIP();
    IPAddress fixed(gw[0], gw[1], gw[2], 201);
    // DNS explicito: sem isso, fixar o IP deixa o ESP32 sem resolver dominio nenhum
    WiFi.config(fixed, gw, WiFi.subnetMask(), IPAddress(8, 8, 8, 8), gw);
    delay(300);
    myIP = WiFi.localIP().toString();
    configTime(-3 * 3600, 0, "pool.ntp.org", "a.st1.ntp.br");

    Serial.printf("IP %s  gw %s  dns %s\n", myIP.c_str(),
                  WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
    IPAddress probe;
    Serial.printf("DNS api.openai.com -> %s\n",
                  WiFi.hostByName("api.openai.com", probe) ? probe.toString().c_str() : "FALHOU");
    Serial.printf("heap livre: %u bytes\n", ESP.getFreeHeap());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("COZMO", "12345678");
    myIP = WiFi.softAPIP().toString();     // sem internet: so os humores funcionam
  }

  oled.clearDisplay(); oled.setTextSize(1);
  oled.setCursor(0, 20); oled.print(F("abra no celular:"));
  oled.setTextSize(2); oled.setCursor(0, 36); oled.print(myIP);
  oled.display();
  delay(3000);

  web.on("/",      []() { web.send_P(200, "text/html", PAGE); });
  web.on("/state", sendState);
  web.on("/dgkey", []() { web.send(200, "text/plain", DEEPGRAM_KEY); });
  web.on("/cfg", []() {                    // tudo que o voice agent do celular precisa
    JsonDocument d;
    d["dg"] = DEEPGRAM_KEY; d["oa"] = OPENAI_KEY;
    d["lat"] = LAT; d["lon"] = LON; d["city"] = CIDADE;
    String o; serializeJson(d, o);
    web.send(200, "application/json", o);
  });
  web.on("/show", []() {                   // o agente manda texto + cara para o display
    answer = web.arg("t");
    setMood((Mood)constrain(web.arg("m").toInt(), 0, NMOODS - 1));
    tAnsw = millis();                      // anima ANIM_MS, depois mostra o texto
    tTouch = millis();
    web.send(200, "text/plain", "ok");
  });
  web.on("/arm", []() {
    String a = web.arg("a");
    if (a == "wave")       { armStart(WAVE);  setMood(FELIZ); }
    else if (a == "spin")  { armStart(SPIN);  setMood(SURPRESO); }
    else if (a == "dance") { armStart(DANCE); setMood(FELIZ); }
    else if (a == "stop")  { armSeq = nullptr; armWrite(0); }
    tTouch = millis();
    web.send(200, "text/plain", "ok");
  });
  web.on("/spk", []() {                    // agente comecou a responder
    if (mood == PENSANDO) setMood(FELIZ);
    tTouch = millis();
    web.send(200, "text/plain", "ok");
  });
  web.on("/dgurl", []() { web.send(200, "text/plain", DEEPGRAM_URL); });
  web.on("/mood",  []() {
    // w=1: troca "fraca" (ouvindo/pensando) - ignorada se o usuario pediu uma expressao
    if (web.hasArg("w") && millis() < holdUntil) { sendState(); return; }
    int m = constrain(web.arg("m").toInt(), 0, NMOODS - 1);
    setMood((Mood)m); tTouch = millis(); tReact = 0; tAnsw = 0;
    if (web.hasArg("hold")) holdUntil = millis() + constrain(web.arg("hold").toInt(), 0, 60000);
    sendState();
  });
  web.on("/cup", []() {
    const char *err = nullptr;
    if (web.arg("a") == "start") err = cupStart();
    else { cup = CUP_OFF; }
    JsonDocument d;
    d["ok"] = err == nullptr;
    if (err) d["erro"] = err;
    else if (cup != CUP_OFF) d["profundidade_cm"] = round(cupDepth * 10) / 10.0;
    String o; serializeJson(d, o);
    web.send(200, "application/json", o);
  });
  web.on("/rps", []() {
    String a = web.arg("a");
    uint32_t now = millis();
    const char *err = nullptr;
    if (a == "start") {
      rpsPick = esp_random() % 3;                // sorteia ANTES da contagem
      rps = RPS_COUNT; tRps = now; rpsStep = -1;
      tAnsw = 0; tTouch = now;
      armStart(SHAKE);
    } else if (a == "play") {
      int u = -1;
      for (int i = 0; i < 3; i++) if (web.arg("u") == RPS_NAME[i]) u = i;
      if (rps != RPS_REVEAL)  err = "nao tem rodada esperando jogada";
      else if (u < 0)         err = "jogada invalida";
      else {
        rpsUser = u;
        int j = rpsJudge(rpsPick, u);
        rpsOutcome = j == 0 ? 0 : j == 1 ? 1 : -1;
        if (rpsOutcome > 0) { scoreCozmo++; setMood(FELIZ);    armStart(DANCE); tone(SPK, 1500, 200); }
        if (rpsOutcome < 0) { scoreVoce++;  setMood(TRISTE);   tone(SPK, 300, 400); }
        if (rpsOutcome == 0){               setMood(SURPRESO); tone(SPK, 900, 150); }
        holdUntil = now + RPS_SHOW_MS;
        rps = RPS_RESULT; tRps = now; tTouch = now;
      }
    } else if (a == "reset") {
      scoreCozmo = scoreVoce = 0;
    }
    rpsReply(err);
  });
  web.on("/light", []() {                 // c = verde | amarelo | vermelho | todas | apagar | auto
    String c = web.arg("c");
    lightBlink = web.arg("b") == "1";
    if      (c == "verde")    lightMask = 1;
    else if (c == "amarelo")  lightMask = 2;
    else if (c == "vermelho") lightMask = 4;
    else if (c == "todas")    lightMask = 7;
    else if (c == "apagar")   lightMask = 0;
    else                    { lightMask = -1; lightBlink = false; }
    tTouch = millis();
    web.send(200, "text/plain", "ok");
  });
  web.on("/talk",  []() {                  // o celular avisa enquanto a voz esta tocando
    talkUntil = web.arg("on") == "1" ? millis() + 1800 : 0;
    tTouch = millis();
    web.send(200, "text/plain", "ok");
  });
  web.on("/ask",   []() {
    if (!thinking && !askPending) {
      pendingQ = web.arg("q");
      lastQ = pendingQ;
      answer = "";
      answerReady = false;
      askPending = true;
      tTouch = millis();
      tAnsw = 0;
      setMood(PENSANDO);
    }
    web.send(200, "application/json", "{\"ok\":1}");
  });
  web.begin();

  xTaskCreatePinnedToCore(netTask, "net", 20480, NULL, 1, NULL, 0);

  for (int i = 0; i < NP; i++) cur[i] = LOOKS[NEUTRO][i];
  setMood(NEUTRO);
  tTouch = millis();
}

void loop() {
  web.handleClient();
  uint32_t now = millis();

  static uint32_t lastBtn = 0;
  if (!digitalRead(BTN) && now - lastBtn > 350) {
    lastBtn = now; tTouch = now; tReact = now;
    setMood(FELIZ);
  }

  if (autoMode && !thinking && !tAnsw && now - tReact > REACT_MS
      && micLast && now - micLast < 150) {
    tTouch = now; tReact = now;
    setMood(SURPRESO);
  }

  // chegou resposta: ele reage primeiro, so depois mostra o texto
  if (answerReady) {
    answerReady = false;
    tAnsw = now;
    tTouch = now;
    if (mood == PENSANDO) setMood(FELIZ);
  }

  if (tReact && !tAnsw && now - tReact > REACT_MS) { tReact = 0; setMood(NEUTRO); }
  if (tAnsw && now - tAnsw > ANIM_MS + TEXT_MS) { tAnsw = 0; setMood(NEUTRO); }
  if (mood == PENSANDO && !thinking && !tAnsw && now - tTouch > 10000) setMood(NEUTRO);
  if (mood != SONO && !thinking && !tAnsw && now - tTouch > SLEEP_MS) setMood(SONO);

  playSfx();
  leds();
  armTick();
  rpsTick();
  cupTick();

  if (now - tFrame >= FRAME_MS) {
    tFrame = now;
    bool showing = tAnsw && now - tAnsw > ANIM_MS && answer.length();
    if (rps != RPS_OFF) drawRps(); else if (cup != CUP_OFF) drawCup();
    else if (showing) drawText(answer); else drawFace();
  }
}
