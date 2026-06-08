/*
  ============================================================
  SENAI IoT – Grupo 5
  Smart Parking IoT – 2 Vagas + Cancela MG90S
  ESP8266 + Sensores IR FC-51 + LEDs + Servo MG90S via MQTT
  Broker: broker.hivemq.com (público, gratuito)
  ============================================================

  CANCELA MG90S:
  - ABERTA  (90°) quando há pelo menos 1 vaga livre
  - FECHADA (0°)  quando as duas vagas estiverem ocupadas
  - Modo manutenção: abre/fecha manualmente via MQTT
    Publicar "ABRIR"   em →  senai/g93611/grupo5/parking/cancela/cmd
    Publicar "FECHAR"  em →  senai/g93611/grupo5/parking/cancela/cmd
    Publicar "AUTO"    em →  senai/g93611/grupo5/parking/cancela/cmd  (volta ao automático)
  - Status publicado em → senai/g93611/grupo5/parking/cancela/status
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Servo.h>

// ── Wi-Fi ────────────────────────────────────────────────
const char* WIFI_SSID     = "G96201IOT";
const char* WIFI_PASSWORD = "12345678";

// ── MQTT ─────────────────────────────────────────────────
const char* MQTT_BROKER   = "broker.hivemq.com";
const int   MQTT_PORT     = 1883;
const char* CLIENT_ID     = "senai_grupo5_parking_esp8266";

// ── Tópicos MQTT ─────────────────────────────────────────
const char* TOPIC_VAGA1          = "senai/g93611/grupo5/parking/vaga1";
const char* TOPIC_VAGA2          = "senai/g93611/grupo5/parking/vaga2";
const char* TOPIC_TOTAL          = "senai/g93611/grupo5/parking/total_livre";
const char* TOPIC_CANCELA_STATUS = "senai/g93611/grupo5/parking/cancela/status";
const char* TOPIC_CANCELA_CMD    = "senai/g93611/grupo5/parking/cancela/cmd";

// ── Pinos dos Sensores IR (Entrada) ──────────────────────
#define SENSOR_VAGA1   D3   // GPIO0  — Sensor IR Vaga 1
#define SENSOR_VAGA2   D4   // GPIO2  — Sensor IR Vaga 2

// ── Pinos dos LEDs (Saída) ────────────────────────────────
#define LED_VERDE_V1   D5   // GPIO14 — LED Verde Vaga 1
#define LED_VERM_V1    D6   // GPIO12 — LED Vermelho Vaga 1
#define LED_VERDE_V2   D7   // GPIO13 — LED Verde Vaga 2
#define LED_VERM_V2    D8   // GPIO15 — LED Vermelho Vaga 2

// ── Pino do Servo (Cancela MG90S) ────────────────────────
#define SERVO_CANCELA  D9   // GPIO4  — Sinal PWM do Servo MG90S

// ── Ângulos da Cancela ────────────────────────────────────
#define ANGULO_ABERTA   180  // Cancela levantada (barra horizontal ou vertical — ajuste conforme montagem)
#define ANGULO_FECHADA   0  // Cancela abaixada

// ── Intervalo de publicação MQTT ─────────────────────────
const long INTERVAL = 3000; // Publica a cada 3 segundos

// ── Variáveis de controle ─────────────────────────────────
bool estadoAnteriorV1   = true;   // true = livre, false = ocupado
bool estadoAnteriorV2   = true;
bool cancelaAbertaAtual = true;   // Estado atual da cancela
bool modoManutencao     = false;  // false = automático, true = manual
unsigned long lastPublish = 0;

Servo servoCancela;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ── Mover Cancela ─────────────────────────────────────────
void moverCancela(bool abrir) {
  int angulo = abrir ? ANGULO_ABERTA : ANGULO_FECHADA;
  servoCancela.write(angulo);
  cancelaAbertaAtual = abrir;

  // Aguarda o servo completar o movimento antes de desligar PWM
  delay(500);

  // Publica status da cancela via MQTT
  const char* statusPayload = abrir ? "ABERTA" : "FECHADA";
  mqtt.publish(TOPIC_CANCELA_STATUS, statusPayload, true); // retain=true

  Serial.print(F("[CANCELA] "));
  Serial.print(abrir ? "ABERTA" : "FECHADA");
  Serial.print(F(" | Modo: "));
  Serial.println(modoManutencao ? "MANUTENCAO" : "AUTOMATICO");
}

// ── Callback MQTT (recebe comandos de manutenção) ─────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.toUpperCase();
  msg.trim();

  Serial.print(F("[MQTT] CMD recebido → "));
  Serial.println(msg);

  if (String(topic) == TOPIC_CANCELA_CMD) {
    if (msg == "ABRIR") {
      modoManutencao = true;
      moverCancela(true);
      mqtt.publish(TOPIC_CANCELA_STATUS, "ABERTA (MANUTENCAO)", true);
      Serial.println(F("[MANUTENCAO] Cancela aberta manualmente."));
    }
    else if (msg == "FECHAR") {
      modoManutencao = true;
      moverCancela(false);
      mqtt.publish(TOPIC_CANCELA_STATUS, "FECHADA (MANUTENCAO)", true);
      Serial.println(F("[MANUTENCAO] Cancela fechada manualmente."));
    }
    else if (msg == "AUTO") {
      modoManutencao = false;
      Serial.println(F("[MANUTENCAO] Modo automático reativado."));
      // Força reavaliação imediata no próximo loop
    }
  }
}

// ── Conexão Wi-Fi ────────────────────────────────────────
void connectWiFi() {
  Serial.println(F("\n[WiFi] Iniciando conexão..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    tentativas++;
    if (tentativas >= 30) {
      Serial.println(F("\n[WiFi] TIMEOUT! Reiniciando..."));
      delay(2000);
      ESP.restart();
    }
  }
  Serial.println(F("\n[WiFi] Conectado! IP:"));
  Serial.println(WiFi.localIP());
}

// ── Conexão MQTT ─────────────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print(F("[MQTT] Conectando ao broker..."));
    if (mqtt.connect(CLIENT_ID)) {
      Serial.println(F(" OK!"));
      // Assina o tópico de comandos da cancela
      mqtt.subscribe(TOPIC_CANCELA_CMD);
      Serial.println(F("[MQTT] Inscrito em: cancela/cmd"));
    } else {
      Serial.print(F(" Falha, rc="));
      Serial.println(mqtt.state());
      delay(5000);
    }
  }
}

// ── Atualiza os LEDs de uma vaga ─────────────────────────
void atualizarLEDs(int pinoVerde, int pinoVermelho, bool livre) {
  digitalWrite(pinoVerde,    livre ? HIGH : LOW);
  digitalWrite(pinoVermelho, livre ? LOW  : HIGH);
}

// ── Publica o status de uma vaga via MQTT ────────────────
void publicarStatus(const char* topico, bool livre, int numVaga) {
  String payload = "{";
  payload += "\"vaga\":"    + String(numVaga)                        + ",";
  payload += "\"status\":\"" + String(livre ? "LIVRE" : "OCUPADO") + "\",";
  payload += "\"livre\":"   + String(livre ? "true" : "false")       + "}";
  mqtt.publish(topico, payload.c_str());
  Serial.print(F("[MQTT] PUB → "));
  Serial.print(topico);
  Serial.print(F(" : "));
  Serial.println(payload);
}

// ── Setup ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("=== Smart Parking IoT – 2 Vagas + Cancela MG90S ==="));

  // Pinos dos Sensores IR como entrada
  pinMode(SENSOR_VAGA1, INPUT);
  pinMode(SENSOR_VAGA2, INPUT);

  // Pinos dos LEDs como saída
  pinMode(LED_VERDE_V1, OUTPUT);
  pinMode(LED_VERM_V1,  OUTPUT);
  pinMode(LED_VERDE_V2, OUTPUT);
  pinMode(LED_VERM_V2,  OUTPUT);

  // Inicializa o Servo da Cancela
  servoCancela.attach(SERVO_CANCELA);
  moverCancela(true); // Cancela inicia ABERTA (vagas disponíveis)
  Serial.println(F("[CANCELA] Servo MG90S inicializado — posição ABERTA"));

  // Estado inicial: todas as vagas livres (verde aceso)
  atualizarLEDs(LED_VERDE_V1, LED_VERM_V1, true);
  atualizarLEDs(LED_VERDE_V2, LED_VERM_V2, true);

  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

// ── Loop principal ───────────────────────────────────────
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected())             connectMQTT();
  mqtt.loop();

  // Leitura dos sensores IR
  // FC-51: HIGH = sem objeto (LIVRE) | LOW = objeto detectado (OCUPADO)
  bool v1Livre = (digitalRead(SENSOR_VAGA1) == HIGH);
  bool v2Livre = (digitalRead(SENSOR_VAGA2) == HIGH);

  // Atualiza LEDs imediatamente ao detectar mudança
  if (v1Livre != estadoAnteriorV1) {
    atualizarLEDs(LED_VERDE_V1, LED_VERM_V1, v1Livre);
    estadoAnteriorV1 = v1Livre;
    Serial.print(F("[VAGA 1] Estado alterado: "));
    Serial.println(v1Livre ? "LIVRE" : "OCUPADO");
  }
  if (v2Livre != estadoAnteriorV2) {
    atualizarLEDs(LED_VERDE_V2, LED_VERM_V2, v2Livre);
    estadoAnteriorV2 = v2Livre;
    Serial.print(F("[VAGA 2] Estado alterado: "));
    Serial.println(v2Livre ? "LIVRE" : "OCUPADO");
  }

  // ── Lógica da Cancela (somente no modo automático) ──────
  if (!modoManutencao) {
    bool deveAbrirCancela = (v1Livre || v2Livre); // Abre se houver ao menos 1 vaga livre
    if (deveAbrirCancela != cancelaAbertaAtual) {
      moverCancela(deveAbrirCancela);
      Serial.print(F("[AUTO] Cancela "));
      Serial.print(deveAbrirCancela ? "ABERTA" : "FECHADA");
      Serial.println(F(" — vagas livres mudaram."));
    }
  }

  // Publicação periódica via MQTT a cada INTERVAL ms
  unsigned long now = millis();
  if (now - lastPublish >= INTERVAL) {
    lastPublish = now;

    publicarStatus(TOPIC_VAGA1, v1Livre, 1);
    publicarStatus(TOPIC_VAGA2, v2Livre, 2);

    // Publica total de vagas livres
    int totalLivre = (int)v1Livre + (int)v2Livre;
    mqtt.publish(TOPIC_TOTAL, String(totalLivre).c_str());

    // Publica status da cancela periodicamente
    String statusCancela = "";
    if (modoManutencao) {
      statusCancela = cancelaAbertaAtual ? "ABERTA (MANUTENCAO)" : "FECHADA (MANUTENCAO)";
    } else {
      statusCancela = cancelaAbertaAtual ? "ABERTA" : "FECHADA";
    }
    mqtt.publish(TOPIC_CANCELA_STATUS, statusCancela.c_str(), true);

    Serial.println(F("────────────────────────────"));
    Serial.print(F("[VAGA 1]: "));   Serial.println(v1Livre ? "LIVRE" : "OCUPADO");
    Serial.print(F("[VAGA 2]: "));   Serial.println(v2Livre ? "LIVRE" : "OCUPADO");
    Serial.print(F("[TOTAL LIVRE]: ")); Serial.println(totalLivre);
    Serial.print(F("[CANCELA]: "));  Serial.println(statusCancela);
    Serial.print(F("[MODO]: "));     Serial.println(modoManutencao ? "MANUTENCAO" : "AUTOMATICO");
    Serial.println(F("────────────────────────────"));
  }
}
