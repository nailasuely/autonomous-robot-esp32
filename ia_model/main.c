// Executar no Arduino IDE com o arq ia_model.h no mesmo diretório

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WiFiUdp.h>         
#include <HTTPClient.h>      
#include <ArduinoJson.h>    
#include "ia_model.h"        


#define ADC1_0 36 // Sensor de Temperatura 
#define ADC1_3 39 // Sensor de Umidade
#define ADC1_6 34 // Sensor de Gás
#define ADC1_7 35 // Sensor de Luz (Fotorresistor)
#define BUTTON 21 // Botão de controle de sistema


const char* ssid = "NOME"; 
const char* password = "SENHA"; 


const char* BROKER_IP = "IP"; 
const int BROKER_DATA_PORT = 9998;    
const int BROKER_COMMAND_PORT = 9999;  


const char* botToken = "7515864309:AAH6vmNAB6jFyaUwMjr-88lPAAXQrh07ZIo"; // Seu token do bot
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);
unsigned long lastTimeBotRan = 0;
const unsigned long botInterval = 1000; 


#define MAX_TEMPERATURE_C 50.0f    
#define MAX_GAS_PPM       217.79f  
#define MAX_LIGHT_CD      1086.46f 


#define MAX_TEMPERATURE_READING 50.0f  
#define MAX_GAS_READING         217.79f 
#define MAX_LIGHT_READING       1086.46f 



unsigned long lastSensorReadAndSend = 0;
const unsigned long SENSOR_READ_INTERVAL = 500; 
unsigned long lastAINNPrediction = 0;
const unsigned long AI_PREDICTION_INTERVAL = 1000; 

volatile bool buttonPressed = false;
bool systemOn = true; 

// --- Comunicação TCP com Broker ---
WiFiClient brokerClient;
const char* DEVICE_NAME = "RoboExplorador"; 


unsigned long lastPhotoCommandSent = 0;
const unsigned long PHOTO_COMMAND_COOLDOWN = 30000; 



void IRAM_ATTR handleButtonInterrupt();
void connectWiFi();
void connectBrokerTCP();
void handleBrokerCommands();
void sendDataToBrokerUDP(float temp, float hum, float gas, float lux, float life_chance, bool system_on, const char* terrain_status);
void handleTelegramMessages();
void sendTelegramLifeMessage(float temp, float hum, float gas, float lux, float life_chance, const char* terrain_status);
void sendTakePhotoCommandToBroker(); 

float relu(float x) { return x > 0 ? x : 0; }
float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

float model_predict(const float x[INPUT_SIZE]) {
  float hidden[HIDDEN_SIZE];
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    hidden[i] = b1[i];
    for (int j = 0; j < INPUT_SIZE; ++j) {
      hidden[i] += x[j] * W1[j][i];
    }
    hidden[i] = relu(hidden[i]);
  }

  float output = b2;
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    output += hidden[i] * W2[i];
  }
  return sigmoid(output);
}

void normalize_readings(float input[INPUT_SIZE]) {
  input[0] /= MAX_TEMPERATURE_READING;
  input[1] /= 100.0f; 
  input[2] /= MAX_GAS_READING;
  input[3] /= MAX_LIGHT_READING;
}


void setup() {
  Serial.begin(9600); 
  delay(100); 

  pinMode(BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON), handleButtonInterrupt, FALLING);

  connectWiFi();

 
  client.setInsecure(); 
  
  connectBrokerTCP();

  Serial.println("Setup completo.");
}

void loop() {
  unsigned long currentTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi desconectado. Tentando reconectar...");
    connectWiFi(); 
  }

  if (!brokerClient.connected()) {
    Serial.println("Conexão TCP com broker perdida. Tentando reconectar...");
    connectBrokerTCP(); 
  } else {
    handleBrokerCommands(); 
  }

  if (currentTime - lastSensorReadAndSend >= SENSOR_READ_INTERVAL) {
    lastSensorReadAndSend = currentTime;

    Serial.println("============================================");

    float temp = 0.0f;
    float hum = 0.0f;
    float gas = 0.0f;
    float lux = 0.0f;
    float chance = 0.0f;
    const char* terrain_status = "Desativado"; 

    if (systemOn) {
      // Leitura dos sensores
      int rawTemp = analogRead(ADC1_0);
      int rawHum  = analogRead(ADC1_3);
      int rawGas  = analogRead(ADC1_6);
      int rawLux  = analogRead(ADC1_7);

      temp = ((float)rawTemp / 4095.0f) * MAX_TEMPERATURE_C;
      hum  = ((float)rawHum / 4095.0f) * 100.0f; // Umidade em %
      gas  = ((float)rawGas / 4095.0f) * MAX_GAS_PPM;
      lux  = ((float)rawLux / 4095.0f) * MAX_LIGHT_CD;

      Serial.printf("Temperatura: %.2f °C\n", temp);
      Serial.printf("Umidade: %.2f %%\n", hum);
      Serial.printf("Gás: %.2f ppm\n", gas);
      Serial.printf("Luminosidade: %.2f cd\n", lux);

      if (currentTime - lastAINNPrediction >= AI_PREDICTION_INTERVAL) {
        lastAINNPrediction = currentTime; // Atualiza o tempo da última predição da IA

        float input[INPUT_SIZE] = { temp, hum, gas, lux };
        normalize_readings(input); // Normaliza para a IA
        chance = model_predict(input); // Executa a predição da IA

        Serial.printf("Chance de vida: %.2f%%\n", chance * 100);
        if (chance >= 0.70) {
          terrain_status = "Propício à vida ✅";
          Serial.println("Status do planeta: Propício à vida ✅");
          
          sendTelegramLifeMessage(temp, hum, gas, lux, chance, terrain_status);

          sendTakePhotoCommandToBroker();
        } else if (chance >= 0.5) {
          terrain_status = "Condição Moderada 🟨";
          Serial.println("Status do planeta: Condição Moderada 🟨");
        } else {
          terrain_status = "Ambiente Hostil ❌";
          Serial.println("Status do planeta: Ambiente Hostil ❌");
        }
      }
    } else {
      Serial.println("Sensores e IA desativados.");
    }

    Serial.print("Status Wi-Fi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado");
    Serial.print("Status do Sistema: ");
    Serial.println(systemOn ? "Ligado" : "Desligado");

    sendDataToBrokerUDP(temp, hum, gas, lux, chance, systemOn, terrain_status);
  }

  if (buttonPressed) {
    buttonPressed = false;
    systemOn = !systemOn; 
    Serial.printf("Sistema agora: %s\n", systemOn ? "Ligado" : "Desligado");

    String notification = "{\"type\":\"notification\",\"name\":\"";
    notification += DEVICE_NAME;
    notification += "\",\"action\":\"";
    notification += (systemOn ? "system_activated" : "system_deactivated");
    notification += "\"}";
    if (brokerClient.connected()) {
      brokerClient.print(notification);
    }
  }

  if (currentTime - lastTimeBotRan > botInterval) {
    handleTelegramMessages();
    lastTimeBotRan = currentTime;
  }
}


void IRAM_ATTR handleButtonInterrupt() {
  static unsigned long lastInterruptTime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime > 200) { // Debounce de 200ms
    lastInterruptTime = currentTime;
    buttonPressed = true;
  }
}


void connectWiFi() {
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  const unsigned long WIFI_TIMEOUT = 20000; // 20 segundos de timeout para conexão Wi-Fi

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startTime > WIFI_TIMEOUT) {
      Serial.println("\nFalha na conexão Wi-Fi após 20 segundos. Reiniciando ESP32...");
      delay(2000); 
      ESP.restart(); 
    }
  }
  Serial.println("\nConectado ao Wi-Fi.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}


void connectBrokerTCP() {
  if (brokerClient.connected()) {
    return; 
  }

  Serial.printf("Conectando ao broker TCP em %s:%d...\n", BROKER_IP, BROKER_COMMAND_PORT);
  if (brokerClient.connect(BROKER_IP, BROKER_COMMAND_PORT)) {
    Serial.println("Conectado ao broker TCP.");
    // Enviar mensagem de registro para o broker
    String registerMsg = "{\"type\":\"register\",\"name\":\"";
    registerMsg += DEVICE_NAME;
    registerMsg += "\"}";
    brokerClient.print(registerMsg);
    Serial.println("Registrado com o broker.");
  } else {
    Serial.println("Falha ao conectar ao broker TCP. Tentando novamente no próximo loop.");
  }
}


void handleBrokerCommands() {
  while (brokerClient.available()) {
    String line = brokerClient.readStringUntil('\n'); 
    Serial.print("Comando recebido do broker: ");
    Serial.println(line);

    StaticJsonDocument<200> doc; // Tamanho do buffer JSON 
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
      Serial.print(F("deserializeJson() falhou: "));
      Serial.println(error.c_str());
      return;
    }

    if (doc.containsKey("command") && doc["command"].is<const char*>()) {
        const char* command_type = doc["command"].as<const char*>();

        if (strcmp(command_type, "toggle_system_state") == 0) {
            systemOn = !systemOn;
            Serial.printf("Comando remoto: Sistema agora: %s\n", systemOn ? "Ligado" : "Desligado");
        } else {
            Serial.printf("Comando desconhecido: %s\n", command_type);
        }
    } else {
        Serial.println("Comando JSON inválido ou formato inesperado.");
    }
  }
}


void sendDataToBrokerUDP(float temp, float hum, float gas, float lux, float life_chance, bool system_on, const char* terrain_status) {
  WiFiUDP udp;
  char jsonBuffer[512]; 
  StaticJsonDocument<512> doc;
  doc["source"] = DEVICE_NAME;
  doc["type"] = "sensor_data";

  JsonObject data = doc.createNestedObject("data");
  data["temp"] = temp;
  data["hum"] = hum;
  data["gas"] = gas;
  data["lux"] = lux;
  data["life_chance"] = life_chance;
  data["terrain_status"] = terrain_status;
  data["system_on"] = system_on;

  serializeJson(doc, jsonBuffer);

  udp.beginPacket(BROKER_IP, BROKER_DATA_PORT);
  udp.print(jsonBuffer);
  udp.endPacket();

}


void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;

      if (text == "/start") {
        bot.sendMessage(chat_id,
          "🛡 *Orisa Online!*\n\n"
          "Sou sua IA de defesa ambiental.\n"
          "Estou aqui para monitorar o ambiente e garantir que tudo esteja sob controle. 💚\n\n"
          "📋 *Comandos disponíveis:*\n"
          "🔍 /status – Verificar estado do sistema\n"
          "📡 /sensores – Dados ambientais atuais\n"
          "🧭 /ajuda – Lista de comandos\n",
          "Markdown");
      }
      else if (text == "/status") {
        String status = systemOn ? "🟢 *Sistema:* Ligado" : "🔴 *Sistema:* Desligado";
        String wifiStatus = WiFi.status() == WL_CONNECTED
          ? "📶 *Wi-Fi:* Conectado \n🌐 *IP:* " + WiFi.localIP().toString()
          : "📶 *Wi-Fi:* Desconectado";

        bot.sendMessage(chat_id,
          "📊 *Status do Sistema* \n\n" +
          status + " \n" + wifiStatus,
          "Markdown");
      }
      else if (text == "/sensores") {
        int rawTemp = analogRead(ADC1_0);
        int rawHum  = analogRead(ADC1_3);
        int rawGas  = analogRead(ADC1_6);
        int rawLux  = analogRead(ADC1_7);

        float temp = ((float)rawTemp / 4095.0f) * MAX_TEMPERATURE_C;
        float hum  = ((float)rawHum / 4095.0f) * 100.0f;
        float gas  = ((float)rawGas / 4095.0f) * MAX_GAS_PPM;
        float lux  = ((float)rawLux / 4095.0f) * MAX_LIGHT_CD;

        float input[INPUT_SIZE] = { temp, hum, gas, lux };
        normalize_readings(input);
        float chance = model_predict(input);

        String statusPlaneta;
        if (chance >= 0.70)
          statusPlaneta = "🟢 *Propício à vida* ✅";
        else if (chance >= 0.5)
          statusPlaneta = "🟡 *Condição Moderada* ⚠️";
        else
          statusPlaneta = "🔴 *Ambiente Hostil* ❌";

        String message = "📡 *Leituras Atuais dos Sensores:* \n\n";
        message += "🌡 *Temperatura:* " + String(temp, 2) + " °C \n";
        message += "💧 *Umidade:* " + String(hum, 2) + " % \n";
        message += "🧪 *Gás:* " + String(gas, 2) + " ppm \n";
        message += "💡 *Luminosidade:* " + String(lux, 2) + " cd \n \n";
        message += "🔬 *Chance de vida:* " + String(chance * 100, 2) + " % \n";
        message += "🛰 *Status do planeta:* " + statusPlaneta;

        bot.sendMessage(chat_id, message, "Markdown");
      }
      else if (text == "/ajuda") {
        bot.sendMessage(chat_id,
          "📘 *Ajuda Orisa*\n\n"
          "Use os comandos abaixo para interagir comigo:\n"
          "🔍 /status – Ver estado do sistema\n"
          "📡 /sensores – Ver leituras dos sensores\n"
          "🧭 /ajuda – Mostrar esta mensagem novamente\n",
          "Markdown");
      }
      else {
        bot.sendMessage(chat_id,
          "❌ *Comando não reconhecido!*\n\n"
          "Use /ajuda para ver a lista de comandos disponíveis.",
          "Markdown");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

void sendTelegramLifeMessage(float temp, float hum, float gas, float lux, float life_chance, const char* terrain_status) {
  static bool lastStatusWasPropicio = false;
  static unsigned long lastTelegramAutoSendTime = 0;
  const unsigned long TELEGRAM_AUTO_SEND_INTERVAL = 60000; 

  if (life_chance >= 0.70) {
    if (!lastStatusWasPropicio || (millis() - lastTelegramAutoSendTime >= TELEGRAM_AUTO_SEND_INTERVAL)) {
      lastStatusWasPropicio = true;
      lastTelegramAutoSendTime = millis();

      String message = "✨ *CONDIÇÕES PROPÍCIAS À VIDA DETECTADAS!* ✨ \n \n";
      message += "📡 *Leituras Atuais dos Sensores:* \n \n";
      message += "🌡 *Temperatura:* " + String(temp, 2) + " °C \n";
      message += "💧 *Umidade:* " + String(hum, 2) + " % \n";
      message += "🧪 *Gás:* " + String(gas, 2) + " ppm \n";
      message += "💡 *Luminosidade:* " + String(lux, 2) + " cd \n\n";
      message += "🔬 *Probabilidade de vida:* " + String(life_chance * 100, 2) + " % \n";
      message += "🛰 *Status do planeta:* " + String(terrain_status);


      if (bot.messages[0].chat_id.length() > 0) { // Usa o chat_id da última mensagem recebida
          bot.sendMessage(bot.messages[0].chat_id, message, "Markdown");
          Serial.println("Mensagem de 'Propício à vida' enviada via Telegram!");
      } else {
          Serial.println("Nenhum chat ID conhecido para enviar a mensagem automática do Telegram.");
      }
    }
  } else {
    lastStatusWasPropicio = false; 
  }
}


void sendTakePhotoCommandToBroker() {
  unsigned long currentTime = millis();
  if (currentTime - lastPhotoCommandSent >= PHOTO_COMMAND_COOLDOWN) {
    if (brokerClient.connected()) {
      String commandMsg = "{\"type\":\"command\",\"command_type\":\"take_photo\"}";
      brokerClient.print(commandMsg);
      Serial.println("Comando 'take_photo' enviado ao broker.");
      lastPhotoCommandSent = currentTime; 
    } else {
      Serial.println("Não foi possível enviar comando 'take_photo': Broker não conectado.");
    }
  }
}
