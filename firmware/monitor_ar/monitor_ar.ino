/**
 * @file monitor_ar.ino
 * @brief Monitor e controle da qualidade do ar interno com ESP32 e MQTT.
 *
 * Projeto da disciplina Objetos Inteligentes Conectados (ADS - Universidade
 * Presbiteriana Mackenzie). Alinhado ao ODS 3 (Saúde e Bem-Estar).
 *
 * O que o firmware faz, em ciclos:
 *   1. lê o sensor de CO2 MH-Z19B (UART2) e o DHT22 (temperatura e umidade);
 *   2. classifica o ar em BOM, ATENCAO, SATURADO ou ALARME (Quadro de níveis);
 *   3. aciona os atuadores: relé da ventilação, LEDs em semáforo, buzzer e
 *      display OLED;
 *   4. publica as medições e os estados em um broker MQTT pela internet
 *      (Wi-Fi + TCP/IP) e obedece aos comandos recebidos por MQTT.
 *
 * A mesma lógica roda na placa física e no simulador Wokwi. No Wokwi, o
 * MH-Z19B é emulado por um chip customizado (pasta /wokwi) que responde ao
 * mesmo protocolo serial de 9 bytes do sensor real.
 *
 * Bibliotecas: PubSubClient, ArduinoJson (v7), DHT sensor library for ESPx
 * (DHTesp), Adafruit GFX e Adafruit SSD1306.
 *
 * Autora: Jennifer Herschel Maia - 2026. Licença MIT.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHTesp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// 1. CONFIGURAÇÃO
// ============================================================================

/** 1 = execução no simulador Wokwi (tempos menores para demonstração). */
#define SIMULACAO 1

// --- Rede Wi-Fi (no Wokwi a rede é "Wokwi-GUEST", sem senha, canal 6) ---
const char *WIFI_SSID  = "Wokwi-GUEST";
const char *WIFI_SENHA = "";

// --- Broker MQTT público (TCP, porta 1883) ---
const char *MQTT_HOST  = "broker.hivemq.com";
const uint16_t MQTT_PORTA = 1883;

/** Prefixo único dos tópicos, para não colidir com outros usuários do broker. */
#define TOPICO_BASE "mackenzie/jhm10720667/ar"

// --- Pinos (iguais ao modelo de montagem do artigo) ---
const int PINO_CO2_RX   = 16;  // RX2 do ESP32  <- TX do MH-Z19B
const int PINO_CO2_TX   = 17;  // TX2 do ESP32  -> RX do MH-Z19B
const int PINO_DHT      = 33;  // dados do DHT22
const int PINO_RELE     = 26;  // entrada IN do módulo relé (ventilação)
const int PINO_LED_VERDE    = 27;
const int PINO_LED_AMARELO  = 14;
const int PINO_LED_VERMELHO = 13;
const int PINO_BUZZER   = 4;
const int PINO_BOTAO    = 18;  // botão para GND, pull-up interno
// Display OLED SSD1306 no I2C padrão: SDA = GPIO21, SCL = GPIO22

// --- Tempos (ms) ---
#if SIMULACAO
const uint32_t AQUECIMENTO_CO2_MS = 10UL * 1000;   // real: 3 min
#else
const uint32_t AQUECIMENTO_CO2_MS = 180UL * 1000;
#endif
const uint32_t RECONEXAO_MS     = 10UL * 1000;     // tentativa de reconexão
const uint32_t ESTADO_PERIODICO_MS = 60UL * 1000;  // republica estado
const uint32_t TOQUE_LONGO_MS   = 3000;            // botão: troca de modo
const uint32_t SILENCIO_MS      = 15UL * 60 * 1000;// botão: silencia alarme

// ============================================================================
// 2. TÓPICOS MQTT
// ============================================================================
const char *T_MEDICAO        = TOPICO_BASE "/medicao";
const char *T_VENT_ESTADO    = TOPICO_BASE "/ventilacao/estado";
const char *T_VENT_COMANDO   = TOPICO_BASE "/ventilacao/comando";
const char *T_ALARME_ESTADO  = TOPICO_BASE "/alarme/estado";
const char *T_ALARME_COMANDO = TOPICO_BASE "/alarme/comando";
const char *T_LEDS_ESTADO    = TOPICO_BASE "/leds/estado";
const char *T_LEDS_COMANDO   = TOPICO_BASE "/leds/comando";
const char *T_DISPLAY_ESTADO = TOPICO_BASE "/display/estado";
const char *T_DISPLAY_COMANDO= TOPICO_BASE "/display/comando";
const char *T_CONFIG         = TOPICO_BASE "/config";
const char *T_CONFIG_ATUAL   = TOPICO_BASE "/config/atual";
const char *T_ALERTA         = TOPICO_BASE "/alerta";
const char *T_STATUS         = TOPICO_BASE "/status";
const char *T_PING           = TOPICO_BASE "/ping";
const char *T_PONG           = TOPICO_BASE "/pong";

// ============================================================================
// 3. TIPOS E ESTADO GLOBAL
// ============================================================================

/** Níveis de qualidade do ar (limites relativos à referência externa). */
enum Nivel { BOM = 0, ATENCAO = 1, SATURADO = 2, ALARME = 3 };
const char *NOME_NIVEL[] = {"BOM", "ATENCAO", "SATURADO", "ALARME"};

/** Parâmetros ajustáveis remotamente pelo tópico .../config. */
struct Config {
  int      refExterna  = 420;   // CO2 do ar externo (ppm)
  int      margemAtencao  = 400; // ref + 400 -> ATENCAO
  int      margemSaturado = 700; // ref + 700 -> SATURADO (NBR 17037)
  int      margemAlarme   = 1100;// ref + 1100 -> ALARME
  uint32_t intervaloMs = SIMULACAO ? 5000UL  : 10000UL; // leitura
  uint32_t persistMs   = SIMULACAO ? 15000UL : 60000UL; // CO2 alto seguido
  uint32_t minEstadoMs = SIMULACAO ? 30000UL : 300000UL;// tempo mínimo no estado
  float    umidMin = 40.0, umidMax = 60.0;               // faixa de conforto
} cfg;

WiFiClient   wifiCliente;
PubSubClient mqtt(wifiCliente);
DHTesp       dht;
Adafruit_SSD1306 tela(128, 64, &Wire, -1);
HardwareSerial &serialCO2 = Serial2;

// Últimas leituras
int      co2ppm = -1;          // -1 = sem leitura válida
float    temperatura = NAN, umidade = NAN;
uint32_t tLeituraCO2 = 0, tLeituraDHT = 0; // millis() da detecção
uint32_t seq = 0;              // número da medição
int      falhasCO2 = 0;        // leituras inválidas seguidas
bool     falhaSensor = false;

Nivel    nivel = BOM, nivelAnterior = BOM;
uint32_t inicioNivelAlto = 0;  // quando o CO2 ficou >= SATURADO

// Ventilação
bool     releLigado = false;
bool     modoAuto = true;
uint32_t ultimaMudancaRele = 0;

// Alarme / indicadores
uint32_t silencioAte = 0;      // alarme silenciado até este instante
uint32_t testeBuzzerAte = 0;   // teste de buzzer em andamento
uint32_t testeLedsAte = 0;     // teste de LEDs em andamento
uint32_t mensagemAte = 0;      // mensagem remota no display
char     mensagemDisplay[22] = "";

// Temporizadores
uint32_t ultimaLeitura = 0, ultimaReconexao = 0, ultimoEstado = 0;
uint32_t ultimaTela = 0;
uint32_t botaoPressionadoEm = 0;
bool     botaoAnterior = false, toqueLongoTratado = false;

// ============================================================================
// 4. SENSOR DE CO2 MH-Z19B (protocolo UART de 9 bytes)
// ============================================================================

/** Checksum do MH-Z19B: complemento de 2 da soma dos bytes 1 a 7. */
uint8_t checksumMHZ(const uint8_t *quadro) {
  uint8_t soma = 0;
  for (int i = 1; i < 8; i++) soma += quadro[i];
  return (uint8_t)(0xFF - soma + 1);
}

/**
 * Pede a concentração de CO2 ao sensor (comando 0x86) e valida a resposta.
 * Conversão: CO2 (ppm) = byte2 * 256 + byte3.
 * @return concentração em ppm, ou -1 se não houve resposta válida.
 */
int lerCO2() {
  const uint8_t pedido[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
  while (serialCO2.available()) serialCO2.read();       // limpa sobras
  serialCO2.write(pedido, 9);

  uint8_t resp[9];
  size_t n = 0;
  uint32_t inicio = millis();
  while (n < 9 && millis() - inicio < 200) {             // espera até 200 ms
    if (serialCO2.available()) {
      uint8_t b = serialCO2.read();
      if (n == 0 && b != 0xFF) continue;                 // sincroniza no 0xFF
      resp[n++] = b;
    }
  }
  if (n < 9 || resp[1] != 0x86 || resp[8] != checksumMHZ(resp)) return -1;
  return resp[2] * 256 + resp[3];
}

/** Desliga a autocalibração (ABC), recomendada só com ar renovado diário. */
void configurarABC(bool ligar) {
  uint8_t cmd[9] = {0xFF, 0x01, 0x79, (uint8_t)(ligar ? 0xA0 : 0x00), 0, 0, 0, 0, 0};
  cmd[8] = checksumMHZ(cmd);
  serialCO2.write(cmd, 9);
}

// ============================================================================
// 5. ATUADORES
// ============================================================================

void publicarJSON(const char *topico, JsonDocument &doc, bool retida = false);

/**
 * Liga ou desliga o relé da ventilação e publica o novo estado.
 * @param origem AUTO, COMANDO, BOTAO, FALHA ou INICIO
 * @param id     identificador do comando remoto (para medir o tempo de resposta)
 */
void acionarRele(bool ligar, const char *origem, const char *id = "") {
  digitalWrite(PINO_RELE, ligar ? HIGH : LOW);
  uint32_t tAcao = millis();                    // instante da ação no atuador
  if (ligar != releLigado) ultimaMudancaRele = tAcao;
  bool mudou = (ligar != releLigado);
  releLigado = ligar;
  if (mudou && ligar && !falhaSensor) tone(PINO_BUZZER, 2000, 120); // bipe curto

  JsonDocument doc;
  doc["rele"]   = releLigado ? "ON" : "OFF";
  doc["modo"]   = modoAuto ? "AUTO" : "MANUAL";
  doc["origem"] = origem;
  doc["id"]     = id;
  doc["ms"]     = tAcao;
  publicarJSON(T_VENT_ESTADO, doc, true);
  ultimoEstado = millis();
}

/** Acende o LED do nível atual (semáforo do ar). */
void atualizarLEDs() {
  if (millis() < testeLedsAte) return;          // teste remoto em andamento
  bool verde = false, amarelo = false, vermelho = false;
  if (millis() < AQUECIMENTO_CO2_MS) {
    amarelo = (millis() / 500) % 2;             // pisca durante o aquecimento
  } else if (falhaSensor) {
    vermelho = (millis() / 250) % 2;            // pisca rápido em falha
  } else {
    bool umidForaFaixa = !isnan(umidade) && (umidade < cfg.umidMin || umidade > cfg.umidMax);
    verde    = (nivel == BOM && !umidForaFaixa);
    amarelo  = (nivel == ATENCAO) || (nivel == BOM && umidForaFaixa);
    vermelho = (nivel >= SATURADO);
  }
  digitalWrite(PINO_LED_VERDE, verde);
  digitalWrite(PINO_LED_AMARELO, amarelo);
  digitalWrite(PINO_LED_VERMELHO, vermelho);
}

/** Buzzer: alarme intermitente no nível ALARME, salvo se silenciado. */
void atualizarBuzzer() {
  if (millis() < testeBuzzerAte) return;
  bool tocar = (nivel == ALARME) && millis() > silencioAte;
  static bool tocando = false;
  if (tocar && (millis() / 500) % 2 == 0) {
    if (!tocando) { tone(PINO_BUZZER, 2500); tocando = true; }
  } else if (tocando) {
    noTone(PINO_BUZZER); tocando = false;
  }
}

/** Desenha o display OLED (CO2, temperatura, umidade, ventilação, rede). */
void atualizarTela() {
  tela.clearDisplay();
  tela.setTextColor(SSD1306_WHITE);
  tela.setTextSize(1);
  tela.setCursor(0, 0);
  if (millis() < mensagemAte) {                 // mensagem enviada pelo painel
    tela.println("Mensagem do painel:");
    tela.setTextSize(2);
    tela.setCursor(0, 20);
    tela.println(mensagemDisplay);
    tela.display();
    return;
  }
  if (millis() < AQUECIMENTO_CO2_MS) {
    tela.printf("Aquecendo sensor %lus\n", (AQUECIMENTO_CO2_MS - millis()) / 1000);
  } else if (falhaSensor) {
    tela.println("FALHA NO SENSOR CO2");
  } else {
    tela.setTextSize(2);
    tela.printf("%4d ppm\n", co2ppm);
    tela.setTextSize(1);
  }
  tela.setCursor(0, 20);
  if (!isnan(temperatura)) tela.printf("%.1f C   %.0f %%UR\n", temperatura, umidade);
  tela.printf("Ar: %s\n", NOME_NIVEL[nivel]);
  tela.printf("Vent: %s  %s\n", releLigado ? "LIGADA" : "deslig.", modoAuto ? "AUTO" : "MANUAL");
  tela.setCursor(0, 56);
  tela.printf("WiFi:%s MQTT:%s", WiFi.status() == WL_CONNECTED ? "ok" : "--",
              mqtt.connected() ? "ok" : "--");
  tela.display();
}

// ============================================================================
// 6. LÓGICA DE DECISÃO
// ============================================================================

/** Converte a concentração de CO2 em nível, usando a referência externa. */
Nivel classificar(int ppm) {
  int r = cfg.refExterna;
  if (ppm > r + cfg.margemAlarme)   return ALARME;
  if (ppm > r + cfg.margemSaturado) return SATURADO;
  if (ppm > r + cfg.margemAtencao)  return ATENCAO;
  return BOM;
}

/**
 * Modo automático: liga a ventilação se o ar ficar saturado por cfg.persistMs
 * e desliga quando voltar ao nível BOM. A histerese (liga em SATURADO, desliga
 * só em BOM) e o tempo mínimo em cada estado evitam liga-desliga.
 */
void controleAutomatico() {
  if (!modoAuto) return;
  uint32_t agora = millis();
  bool tempoMinimoOk = (agora - ultimaMudancaRele) >= cfg.minEstadoMs || ultimaMudancaRele == 0;

  if (falhaSensor) {                            // falha: ventila por segurança
    if (!releLigado) acionarRele(true, "FALHA");
    return;
  }
  if (nivel >= SATURADO) {
    if (inicioNivelAlto == 0) inicioNivelAlto = agora;
    if (!releLigado && agora - inicioNivelAlto >= cfg.persistMs && tempoMinimoOk)
      acionarRele(true, "AUTO");
  } else {
    inicioNivelAlto = 0;
    if (releLigado && nivel == BOM && tempoMinimoOk) acionarRele(false, "AUTO");
  }
}

/** Publica um alerta quando o nível muda ou quando há falha. */
void publicarAlerta(const char *tipo, const char *detalhe) {
  JsonDocument doc;
  doc["tipo"] = tipo;
  doc["detalhe"] = detalhe;
  doc["co2"] = co2ppm;
  doc["nivel"] = NOME_NIVEL[nivel];
  doc["ms"] = millis();
  publicarJSON(T_ALERTA, doc);
}

/** Lê os sensores, classifica o ar e publica a medição. */
void cicloDeMedicao() {
  // --- CO2 ---
  int leitura = lerCO2();
  if (leitura >= 0) {
    tLeituraCO2 = millis();                     // instante da detecção
    falhasCO2 = 0;
    if (falhaSensor) { falhaSensor = false; publicarAlerta("SENSOR", "CO2 voltou a responder"); }
    if (millis() >= AQUECIMENTO_CO2_MS) co2ppm = leitura;
  } else if (++falhasCO2 >= 3 && !falhaSensor) {
    falhaSensor = true;
    publicarAlerta("FALHA_SENSOR", "MH-Z19B sem resposta valida 3 vezes");
  }

  // --- temperatura e umidade ---
  TempAndHumidity th = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE) {
    temperatura = th.temperature;
    umidade = th.humidity;
    tLeituraDHT = millis();
  }

  if (co2ppm < 0) return;                       // ainda aquecendo

  nivel = classificar(co2ppm);
  if (nivel != nivelAnterior) {
    publicarAlerta("NIVEL", NOME_NIVEL[nivel]);
    nivelAnterior = nivel;
  }

  // --- publicação (QoS 0) ---
  JsonDocument doc;
  doc["seq"]   = ++seq;
  doc["co2"]   = co2ppm;
  if (isnan(temperatura)) { doc["temp"] = nullptr; doc["umid"] = nullptr; }
  else {
    doc["temp"] = serialized(String(temperatura, 1));
    doc["umid"] = serialized(String(umidade, 1));
  }
  doc["nivel"] = NOME_NIVEL[nivel];
  doc["rele"]  = releLigado ? "ON" : "OFF";
  doc["modo"]  = modoAuto ? "AUTO" : "MANUAL";
  doc["t_co2"] = tLeituraCO2;                   // detecção do MH-Z19B
  doc["t_dht"] = tLeituraDHT;                   // detecção do DHT22
  doc["ms"]    = millis();                      // instante do envio
  publicarJSON(T_MEDICAO, doc);
}

// ============================================================================
// 7. MQTT: PUBLICAÇÃO, COMANDOS E CONEXÃO
// ============================================================================

/** Serializa um JSON e publica (a PubSubClient publica sempre com QoS 0). */
void publicarJSON(const char *topico, JsonDocument &doc, bool retida) {
  if (!mqtt.connected()) return;
  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(topico, (const uint8_t *)buf, n, retida);
}

/** Publica a configuração em uso (mensagem retida). */
void publicarConfig() {
  JsonDocument doc;
  doc["ref"] = cfg.refExterna;
  doc["intervalo_s"] = cfg.intervaloMs / 1000;
  doc["persist_s"] = cfg.persistMs / 1000;
  doc["min_s"] = cfg.minEstadoMs / 1000;
  doc["limite_saturado"] = cfg.refExterna + cfg.margemSaturado;
  doc["limite_alarme"] = cfg.refExterna + cfg.margemAlarme;
  publicarJSON(T_CONFIG_ATUAL, doc, true);
}

/** Confirma a ação de um atuador com o id do comando e o instante da ação. */
void confirmarAcao(const char *topico, const char *chave, const char *valor,
                   const char *id, uint32_t tAcao) {
  JsonDocument doc;
  doc[chave] = valor;
  doc["id"] = id;
  doc["ms"] = tAcao;
  publicarJSON(topico, doc);
}

/**
 * Trata as mensagens recebidas. Os comandos aceitam texto simples ("ON") ou
 * JSON ({"cmd":"ON","id":"abc"}); o id volta na confirmação.
 */
void aoReceberMensagem(char *topico, uint8_t *payload, unsigned int tam) {
  uint32_t tChegada = millis();
  String texto;
  texto.reserve(tam);
  for (unsigned int i = 0; i < tam; i++) texto += (char)payload[i];
  texto.trim();

  JsonDocument doc;
  String cmd = texto, id = "";
  if (texto.startsWith("{") && !deserializeJson(doc, texto)) {
    cmd = doc["cmd"] | "";
    id  = doc["id"]  | "";
  }
  cmd.toUpperCase();

  if (strcmp(topico, T_PING) == 0) {            // sincronização de relógio
    JsonDocument r;
    r["id"] = id.length() ? id : texto;
    r["ms"] = tChegada;
    publicarJSON(T_PONG, r);

  } else if (strcmp(topico, T_VENT_COMANDO) == 0) {
    if (cmd == "AUTO") {
      modoAuto = true;
      acionarRele(releLigado, "COMANDO", id.c_str());
    } else if (cmd == "ON" || cmd == "OFF") {
      modoAuto = false;                         // comando manual sai do automático
      acionarRele(cmd == "ON", "COMANDO", id.c_str());
    }

  } else if (strcmp(topico, T_ALARME_COMANDO) == 0) {
    if (cmd == "TESTE") {
      tone(PINO_BUZZER, 2000, 800);
      testeBuzzerAte = millis() + 800;
      confirmarAcao(T_ALARME_ESTADO, "buzzer", "TESTE", id.c_str(), millis());
    } else if (cmd == "SILENCIAR") {
      silencioAte = millis() + SILENCIO_MS;
      noTone(PINO_BUZZER);
      confirmarAcao(T_ALARME_ESTADO, "buzzer", "SILENCIADO", id.c_str(), millis());
    }

  } else if (strcmp(topico, T_LEDS_COMANDO) == 0 && cmd == "TESTE") {
    digitalWrite(PINO_LED_VERDE, HIGH);
    digitalWrite(PINO_LED_AMARELO, HIGH);
    digitalWrite(PINO_LED_VERMELHO, HIGH);
    testeLedsAte = millis() + 1500;
    confirmarAcao(T_LEDS_ESTADO, "leds", "TESTE", id.c_str(), millis());

  } else if (strcmp(topico, T_DISPLAY_COMANDO) == 0) {
    String msg = texto.startsWith("{") ? String(doc["texto"] | "Ola!") : texto;
    msg.toCharArray(mensagemDisplay, sizeof(mensagemDisplay));
    mensagemAte = millis() + 3000;
    atualizarTela();                            // ação imediata no display
    confirmarAcao(T_DISPLAY_ESTADO, "display", mensagemDisplay, id.c_str(), millis());

  } else if (strcmp(topico, T_CONFIG) == 0 && doc.size() > 0) {
    if (doc["ref"].is<int>())         cfg.refExterna  = doc["ref"].as<int>();
    if (doc["intervalo_s"].is<int>()) cfg.intervaloMs = doc["intervalo_s"].as<uint32_t>() * 1000;
    if (doc["persist_s"].is<int>())   cfg.persistMs   = doc["persist_s"].as<uint32_t>() * 1000;
    if (doc["min_s"].is<int>())       cfg.minEstadoMs = doc["min_s"].as<uint32_t>() * 1000;
    publicarConfig();
  }
}

/** Conecta (ou reconecta) ao Wi-Fi e ao broker sem travar o laço principal. */
void manterConexao() {
  if (millis() - ultimaReconexao < RECONEXAO_MS && ultimaReconexao != 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    ultimaReconexao = millis();
    Serial.println("[WiFi] conectando...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
    return;
  }
  if (mqtt.connected()) return;
  ultimaReconexao = millis();

  String idCliente = "esp32-ar-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("[MQTT] conectando a %s como %s\n", MQTT_HOST, idCliente.c_str());
  // Testamento (Last Will): o broker publica "offline" se a placa sumir.
  if (mqtt.connect(idCliente.c_str(), T_STATUS, 1, true, "offline")) {
    Serial.println("[MQTT] conectado");
    mqtt.publish(T_STATUS, "online", true);
    mqtt.subscribe(T_VENT_COMANDO, 1);
    mqtt.subscribe(T_ALARME_COMANDO, 1);
    mqtt.subscribe(T_LEDS_COMANDO, 1);
    mqtt.subscribe(T_DISPLAY_COMANDO, 1);
    mqtt.subscribe(T_CONFIG, 1);
    mqtt.subscribe(T_PING, 0);
    publicarConfig();
    acionarRele(releLigado, "INICIO");
  } else {
    Serial.printf("[MQTT] falhou, estado %d\n", mqtt.state());
  }
}

// ============================================================================
// 8. BOTÃO LOCAL
// ============================================================================

/** Toque curto: silencia o alarme. Toque longo (3 s): alterna AUTO/MANUAL. */
void lerBotao() {
  bool pressionado = digitalRead(PINO_BOTAO) == LOW;
  uint32_t agora = millis();
  if (pressionado && !botaoAnterior) { botaoPressionadoEm = agora; toqueLongoTratado = false; }
  if (pressionado && !toqueLongoTratado && agora - botaoPressionadoEm >= TOQUE_LONGO_MS) {
    toqueLongoTratado = true;
    modoAuto = !modoAuto;
    acionarRele(releLigado, "BOTAO");
  }
  if (!pressionado && botaoAnterior && !toqueLongoTratado && agora - botaoPressionadoEm > 50) {
    silencioAte = agora + SILENCIO_MS;
    noTone(PINO_BUZZER);
    confirmarAcao(T_ALARME_ESTADO, "buzzer", "SILENCIADO", "botao", agora);
  }
  botaoAnterior = pressionado;
}

// ============================================================================
// 9. SETUP E LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, LOW);                 // ventilação sempre começa desligada
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_AMARELO, OUTPUT);
  pinMode(PINO_LED_VERMELHO, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);

  serialCO2.begin(9600, SERIAL_8N1, PINO_CO2_RX, PINO_CO2_TX);
  configurarABC(false);
  dht.setup(PINO_DHT, DHTesp::DHT22);

  if (!tela.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("[OLED] nao encontrado");
  tela.clearDisplay();
  tela.display();

  WiFi.mode(WIFI_STA);
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceberMensagem);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(5);
  Serial.println("Monitor de qualidade do ar - iniciado");
}

void loop() {
  manterConexao();
  mqtt.loop();
  lerBotao();

  uint32_t agora = millis();
  if (agora - ultimaLeitura >= cfg.intervaloMs) {
    ultimaLeitura = agora;
    cicloDeMedicao();
    controleAutomatico();
  }
  if (agora - ultimoEstado >= ESTADO_PERIODICO_MS) acionarRele(releLigado, "PERIODICO");

  atualizarLEDs();
  atualizarBuzzer();
  if (agora - ultimaTela >= 1000) { ultimaTela = agora; atualizarTela(); }
}
