#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <PubSubClient.h>

// ===============================
// CONFIGURAÇÕES DO WI-FI
// ===============================

const char* ssid = "SUA REDE";
const char* password = "SUA SENHA";

// ===============================
// CONFIGURAÇÕES DO MQTT
// ===============================

const char* MQTT_SERVER = "broker.hivemq.com";   // broker público
const int   MQTT_PORT   = 1883;

// Prefixo único: troque "nathan" se quiser, para não misturar com outros usuários do broker público
#define BASE "satc/iot/nathan"

// Tópicos de publicação (ESP32 -> broker)
const char* T_TEMP         = BASE "/temperatura";
const char* T_UMID         = BASE "/umidade";
const char* T_LUZ          = BASE "/luminosidade";
const char* T_BOTAO        = BASE "/botao";          // publica em /botao/1 ... /botao/4
const char* T_RELE_ESTADO  = BASE "/rele/estado";
const char* T_RGB_ESTADO   = BASE "/ledrgb/estado";
const char* T_STATUS       = BASE "/status";

// Tópicos de comando (broker -> ESP32)
const char* T_RELE_CMD     = BASE "/rele";           // ON / OFF
const char* T_RGB_CMD      = BASE "/ledrgb";         // r,g,b  (ex.: 255,0,128)

const unsigned long INTERVALO_PUBLICACAO = 5000;     // 5 segundos

// ===============================
// PINOS
// ===============================

#define DHT_PIN     33
#define DHT_TIPO    DHT11
#define LDR_PIN     39

#define BOTAO1_PIN  4
#define BOTAO2_PIN  0
#define BOTAO3_PIN  2
#define BOTAO4_PIN  15

#define RELE_PIN    13

#define RGB_R_PIN   27
#define RGB_G_PIN   26
#define RGB_B_PIN   25

// ===============================
// OBJETOS GLOBAIS
// ===============================

DHT dht(DHT_PIN, DHT_TIPO);
WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

bool releLigado = false;
int rgbR = 0, rgbG = 0, rgbB = 0;

// Controle dos botões (publicação só quando o estado muda)
const int botaoPins[4] = {BOTAO1_PIN, BOTAO2_PIN, BOTAO3_PIN, BOTAO4_PIN};
bool botaoLeitura[4]   = {false, false, false, false};
bool botaoEstado[4]    = {false, false, false, false};
unsigned long botaoTempo[4] = {0, 0, 0, 0};

// Controle de tempo do MQTT
unsigned long ultimaPublicacao = 0;
unsigned long ultimaTentativaMQTT = 0;
bool primeiraTentativaMQTT = true;

// ===============================
// FUNÇÕES AUXILIARES MQTT
// ===============================

void publicar(const char* topico, const String& valor, bool retain = false) {
  if (mqtt.connected()) {
    mqtt.publish(topico, valor.c_str(), retain);
  }
}

void publicarEstadoRele() {
  publicar(T_RELE_ESTADO, releLigado ? "ON" : "OFF", true);
}

void publicarEstadoRGB() {
  publicar(T_RGB_ESTADO, String(rgbR) + "," + String(rgbG) + "," + String(rgbB), true);
}

// ===============================
// FUNÇÕES AUXILIARES DE HARDWARE
// ===============================

void aplicarRele(bool ligado) {
  digitalWrite(RELE_PIN, ligado ? HIGH : LOW);
  releLigado = ligado;
}

void aplicarRGB(int r, int g, int b) {
  analogWrite(RGB_R_PIN, 255 - r);
  analogWrite(RGB_G_PIN, 255 - g);
  analogWrite(RGB_B_PIN, 255 - b);
  rgbR = r; rgbG = g; rgbB = b;
}

// ===============================
// PÁGINA PRINCIPAL
// ===============================

void handleRoot() {

  String pagina = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Estação de Monitoramento</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f2f2f2;
      text-align: center;
      margin: 0;
      padding: 30px;
    }
    h1 { color: #222; }
    .card {
      background-color: white;
      padding: 20px;
      margin: 20px auto;
      max-width: 500px;
      border-radius: 10px;
      box-shadow: 0 2px 6px rgba(0,0,0,0.15);
      text-align: left;
    }
    .card h2 { margin-top: 0; text-align: center; }
    .linha { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid #eee; }
    .linha:last-child { border-bottom: none; }
    .valor { font-weight: bold; }
    .botoes-grid { display: flex; justify-content: space-around; margin-top: 10px; }
    .bolinha {
      width: 40px; height: 40px; border-radius: 50%;
      background: #ccc; display: flex; align-items: center; justify-content: center;
      font-weight: bold; color: white; margin: 0 auto 5px auto;
      transition: background 0.2s;
    }
    .bolinha.ativo { background: #2ecc71; }
    .botao-label { text-align: center; font-size: 13px; }
    button {
      padding: 10px 20px; border: none; border-radius: 6px;
      background-color: #3498db; color: white; font-size: 15px; cursor: pointer;
    }
    button.desligado { background-color: #7f8c8d; }
    button:hover { opacity: 0.9; }
    input[type=range] { width: 100%; }
    .preview {
      width: 60px; height: 60px; border-radius: 8px; margin: 10px auto;
      border: 2px solid #ccc;
    }
    .relay-row { display: flex; justify-content: space-between; align-items: center; }
  </style>
</head>
<body>

  <h1>Estação de Monitoramento IoT</h1>

  <div class="card">
    <h2>Sensores</h2>
    <div class="linha"><span>Temperatura</span><span class="valor" id="temp">--</span></div>
    <div class="linha"><span>Umidade</span><span class="valor" id="umid">--</span></div>
    <div class="linha"><span>Luminosidade</span><span class="valor" id="luz">--</span></div>
  </div>

  <div class="card">
    <h2>Botões</h2>
    <div class="botoes-grid">
      <div><div class="bolinha" id="b1">1</div><div class="botao-label">SW1</div></div>
      <div><div class="bolinha" id="b2">2</div><div class="botao-label">SW2</div></div>
      <div><div class="bolinha" id="b3">3</div><div class="botao-label">SW3</div></div>
      <div><div class="bolinha" id="b4">4</div><div class="botao-label">SW4</div></div>
    </div>
  </div>

  <div class="card">
    <h2>Relé</h2>
    <div class="relay-row">
      <span>Estado: <span class="valor" id="releStatus">--</span></span>
      <button id="btnRele" onclick="alternarRele()">Alternar</button>
    </div>
  </div>

  <div class="card">
    <h2>LED RGB</h2>
    <div class="preview" id="preview"></div>
    <label>R: <span id="valR">0</span></label>
    <input type="range" min="0" max="255" value="0" id="sliderR" oninput="editandoRGB = true; atualizarPreview()" onchange="enviarRGB()">
    <label>G: <span id="valG">0</span></label>
    <input type="range" min="0" max="255" value="0" id="sliderG" oninput="editandoRGB = true; atualizarPreview()" onchange="enviarRGB()">
    <label>B: <span id="valB">0</span></label>
    <input type="range" min="0" max="255" value="0" id="sliderB" oninput="editandoRGB = true; atualizarPreview()" onchange="enviarRGB()">
  </div>

<script>
  let releLigado = false;
  let editandoRGB = false;   // evita sobrescrever os sliders enquanto o usuário arrasta

  function atualizarPreview() {
    const r = document.getElementById('sliderR').value;
    const g = document.getElementById('sliderG').value;
    const b = document.getElementById('sliderB').value;
    document.getElementById('valR').innerText = r;
    document.getElementById('valG').innerText = g;
    document.getElementById('valB').innerText = b;
    document.getElementById('preview').style.backgroundColor = `rgb(${r},${g},${b})`;
  }

  function enviarRGB() {
    const r = document.getElementById('sliderR').value;
    const g = document.getElementById('sliderG').value;
    const b = document.getElementById('sliderB').value;
    fetch(`/rgb?r=${r}&g=${g}&b=${b}`)
      .catch(err => console.log('Erro ao enviar cor:', err))
      .finally(() => { editandoRGB = false; });
  }

  // Atualiza os sliders e a prévia com a cor atual do ESP32 (ex.: alterada via MQTT)
  function atualizarRGB(rgb) {
    if (editandoRGB || !rgb) return;
    document.getElementById('sliderR').value = rgb.r;
    document.getElementById('sliderG').value = rgb.g;
    document.getElementById('sliderB').value = rgb.b;
    atualizarPreview();
  }

  function alternarRele() {
    const novoEstado = releLigado ? 0 : 1;
    fetch(`/rele?estado=${novoEstado}`)
      .then(r => r.json())
      .then(dados => atualizarRele(dados.rele));
  }

  function atualizarRele(ligado) {
    releLigado = ligado;
    document.getElementById('releStatus').innerText = ligado ? 'Ligado' : 'Desligado';
    const btn = document.getElementById('btnRele');
    btn.classList.toggle('desligado', !ligado);
  }

  function atualizarStatus() {
    fetch('/dados')
      .then(r => r.json())
      .then(d => {
        document.getElementById('temp').innerText = (d.temperatura === null ? 'erro' : d.temperatura + ' °C');
        document.getElementById('umid').innerText = (d.umidade === null ? 'erro' : d.umidade + ' %');
        document.getElementById('luz').innerText = d.luminosidade_pct + ' % (' + d.luminosidade_raw + ')';

        document.getElementById('b1').classList.toggle('ativo', d.botao1);
        document.getElementById('b2').classList.toggle('ativo', d.botao2);
        document.getElementById('b3').classList.toggle('ativo', d.botao3);
        document.getElementById('b4').classList.toggle('ativo', d.botao4);

        atualizarRele(d.rele);
        atualizarRGB(d.rgb);
      })
      .catch(err => console.log('Erro ao buscar dados:', err));
  }

  atualizarStatus();
  setInterval(atualizarStatus, 2000);
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", pagina);
}

// ===============================
// /dados -> JSON com leituras atuais
// ===============================

void handleDados() {

  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  int luzRaw = analogRead(LDR_PIN);
  int luzPct = map(luzRaw, 0, 4095, 0, 100);

  bool b1 = digitalRead(BOTAO1_PIN) == LOW;
  bool b2 = digitalRead(BOTAO2_PIN) == LOW;
  bool b3 = digitalRead(BOTAO3_PIN) == LOW;
  bool b4 = digitalRead(BOTAO4_PIN) == LOW;

  String json = "{";
  json += "\"temperatura\":" + (isnan(temperatura) ? String("null") : String(temperatura, 1)) + ",";
  json += "\"umidade\":" + (isnan(umidade) ? String("null") : String(umidade, 1)) + ",";
  json += "\"luminosidade_raw\":" + String(luzRaw) + ",";
  json += "\"luminosidade_pct\":" + String(luzPct) + ",";
  json += "\"botao1\":" + String(b1 ? "true" : "false") + ",";
  json += "\"botao2\":" + String(b2 ? "true" : "false") + ",";
  json += "\"botao3\":" + String(b3 ? "true" : "false") + ",";
  json += "\"botao4\":" + String(b4 ? "true" : "false") + ",";
  json += "\"rele\":" + String(releLigado ? "true" : "false") + ",";
  json += "\"rgb\":{\"r\":" + String(rgbR) + ",\"g\":" + String(rgbG) + ",\"b\":" + String(rgbB) + "}";
  json += "}";

  server.send(200, "application/json", json);
}

// ===============================
// /rele?estado=1|0 -> liga/desliga o relé
// ===============================

void handleRele() {
  if (server.hasArg("estado")) {
    String estado = server.arg("estado");
    aplicarRele(estado == "1" || estado == "true" || estado == "on");
    publicarEstadoRele();   // mantém o MQTT sincronizado com a página
  }
  server.send(200, "application/json", "{\"rele\":" + String(releLigado ? "true" : "false") + "}");
}

// ===============================
// /rgb?r=&g=&b= -> ajusta a cor do LED RGB
// ===============================

void handleRGB() {
  int r = rgbR, g = rgbG, b = rgbB;
  if (server.hasArg("r")) r = constrain(server.arg("r").toInt(), 0, 255);
  if (server.hasArg("g")) g = constrain(server.arg("g").toInt(), 0, 255);
  if (server.hasArg("b")) b = constrain(server.arg("b").toInt(), 0, 255);

  aplicarRGB(r, g, b);
  publicarEstadoRGB();      // mantém o MQTT sincronizado com a página

  server.send(200, "application/json",
    "{\"r\":" + String(rgbR) + ",\"g\":" + String(rgbG) + ",\"b\":" + String(rgbB) + "}");
}

// ===============================
// MQTT: RECEBIMENTO DE COMANDOS
// ===============================

void callbackMQTT(char* topic, byte* payload, unsigned int length) {

  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  String up = msg;
  up.toUpperCase();
  String t = String(topic);

  Serial.print("MQTT recebido [");
  Serial.print(t);
  Serial.print("]: ");
  Serial.println(msg);

  // ----- Relé: ON / OFF -----
  if (t == T_RELE_CMD) {
    if (up == "ON" || up == "1" || up == "TRUE" || up == "LIGAR") {
      aplicarRele(true);
    } else if (up == "OFF" || up == "0" || up == "FALSE" || up == "DESLIGAR") {
      aplicarRele(false);
    }
    publicarEstadoRele();
  }

  // ----- LED RGB: "r,g,b" ou OFF -----
  else if (t == T_RGB_CMD) {
    int r, g, b;
    if (up == "OFF") {
      aplicarRGB(0, 0, 0);
    } else if (sscanf(msg.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
      aplicarRGB(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
    }
    publicarEstadoRGB();
  }
}

// ===============================
// MQTT: CONEXÃO (sem travar o servidor HTTP)
// ===============================

void manterMQTT() {

  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) return;

  if (!primeiraTentativaMQTT && millis() - ultimaTentativaMQTT < 5000) return;
  primeiraTentativaMQTT = false;
  ultimaTentativaMQTT = millis();

  Serial.print("Conectando ao broker MQTT...");

  String clientId = "esp32-nathan-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  // Last Will: se o ESP32 cair, o broker publica "offline" no tópico de status
  if (mqtt.connect(clientId.c_str(), T_STATUS, 0, true, "offline")) {
    Serial.println(" conectado!");
    publicar(T_STATUS, "online", true);
    mqtt.subscribe(T_RELE_CMD);
    mqtt.subscribe(T_RGB_CMD);
    publicarEstadoRele();
    publicarEstadoRGB();
  } else {
    Serial.print(" falhou (rc=");
    Serial.print(mqtt.state());
    Serial.println("). Nova tentativa em 5 s.");
  }
}

// ===============================
// MQTT: PUBLICAÇÃO PERIÓDICA DOS SENSORES
// ===============================

void publicarSensores() {

  if (!mqtt.connected()) return;
  if (millis() - ultimaPublicacao < INTERVALO_PUBLICACAO) return;
  ultimaPublicacao = millis();

  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  int luzRaw = analogRead(LDR_PIN);
  int luzPct = map(luzRaw, 0, 4095, 0, 100);

  if (!isnan(temperatura)) publicar(T_TEMP, String(temperatura, 1));
  if (!isnan(umidade))     publicar(T_UMID, String(umidade, 1));
  publicar(T_LUZ, String(luzPct));

  Serial.println("Sensores publicados via MQTT.");
}

// ===============================
// MQTT: BOTÕES (publica apenas quando o estado muda)
// ===============================

void verificarBotoes() {

  for (int i = 0; i < 4; i++) {
    bool pressionado = (digitalRead(botaoPins[i]) == LOW);

    if (pressionado != botaoLeitura[i]) {
      botaoLeitura[i] = pressionado;
      botaoTempo[i] = millis();
    }

    // debounce de 30 ms
    if (millis() - botaoTempo[i] > 30 && pressionado != botaoEstado[i]) {
      botaoEstado[i] = pressionado;
      String topico = String(T_BOTAO) + "/" + String(i + 1);
      publicar(topico.c_str(), pressionado ? "PRESSIONADO" : "SOLTO");
    }
  }
}

// ===============================
// CONFIGURAÇÃO
// ===============================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("   ESTACAO DE MONITORAMENTO");
  Serial.println("        (HTTP + MQTT)");
  Serial.println("================================");

  pinMode(BOTAO1_PIN, INPUT_PULLUP);
  pinMode(BOTAO2_PIN, INPUT_PULLUP);
  pinMode(BOTAO3_PIN, INPUT_PULLUP);
  pinMode(BOTAO4_PIN, INPUT_PULLUP);

  pinMode(RELE_PIN, OUTPUT);
  aplicarRele(false);

  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT);
  aplicarRGB(0, 0, 0);

  dht.begin();

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Wi-Fi conectado!");
  Serial.print("Endereco IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/dados", HTTP_GET, handleDados);
  server.on("/rele", HTTP_GET, handleRele);
  server.on("/rgb", HTTP_GET, handleRGB);

  server.begin();

  Serial.println("Servidor HTTP iniciado!");
  Serial.println("Acesse o IP acima pelo navegador.");

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(callbackMQTT);
}

// ===============================
// LOOP PRINCIPAL
// ===============================

void loop() {
  server.handleClient();   // Entrega 1 (HTTP)

  manterMQTT();            // Entrega 2 (MQTT)
  publicarSensores();
  verificarBotoes();
}
