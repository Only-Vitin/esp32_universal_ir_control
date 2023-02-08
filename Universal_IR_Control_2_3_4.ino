/*
   @description Project for universal infrared control
   @date 01/05/2022
   @author Joao Vitor/Only-Vitin
   @version 2.3.4
*/

#include <string.h>
#include <WiFi.h>
#include <Redis.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>

// PINS
#define BUZZER_PIN 19
#define LED_PILOT_PIN 4
#define LED_STATUS_PIN 13

// GLOBAL CONFIG NAME BUTTON TO SAVE
String nameButton = "";
boolean nameState = false;

// GLOBAL CONFIG MODE FOR SAVE
boolean recording = false;

// WIFI
#define WIFI_NAME "#foraDS"
#define WIFI_PASS "10101010"

// REDIS
#define REDIS_ADDR "infraproject.ddns.net"
#define REDIS_PORT 6379
#define REDIS_PASSWORD "kgb8y2kk"

WiFiClient redisConn;
Redis redis(redisConn);

// MQTT
#define BROKER_ID "e042cdb9-1fc0-4bc5-a52d-b20542af9bd6"
#define BROKER_ADDRESS "infraproject.ddns.net"
#define BROKER_PORT 8884
#define BROKER_USER "admin"
#define BROKER_PASS "kgb8y2kk"

#define TOPIC_SUBSCRIBLE "aircontrol/cmnd/00"
#define TOPIC_PUBLISH_STATUS "aircontrol/status/00"

WiFiClient mqttConn;
PubSubClient mqtt(mqttConn);

// DHT
#define DHT_PIN 18
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// IR
#define IR_RECV_PIN 5
#define IR_SEND_PIN 2
const uint16_t kFrequency = 38000;

IRsend irsend(IR_SEND_PIN);
IRrecv irrecv(IR_RECV_PIN);
decode_results results;

void setup() {
  delay(500);
  setupBegin();
  setupPins();
  Serial.println("--- INIT SETUP");
  setupWifi();
  setupmqtt();
  setupRedis();
  Serial.println("--- END SETUP");
  digitalWrite(LED_PILOT_PIN, true);
}

void setupBegin() {
  Serial.begin(115200);
  dht.begin();
  irsend.begin();
}

void setupWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_NAME, WIFI_PASS);
    Serial.print("Connecting Wifi ");
    while (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_PILOT_PIN, true);
      Serial.print('.');
      delay(250);
      digitalWrite(LED_PILOT_PIN, false);
      delay(250);
    }
    Serial.println();
    Serial.println("Connected Wifi");
    Serial.print("IP ");
    Serial.println(WiFi.localIP());
  }
}

void setupmqtt() {
  if (!mqtt.connected()) {
    mqtt.setServer(BROKER_ADDRESS, BROKER_PORT);
    mqtt.setCallback(callbackmqtt);
    Serial.println("---");
    Serial.print("Connecting mqtt ");
    while (!mqtt.connected()) {
      if (mqtt.connect(BROKER_ID, BROKER_USER, BROKER_PASS)) {
        Serial.println();
        Serial.println("Connected mqtt");
        Serial.print("DNS ");
        Serial.println(BROKER_ADDRESS);
        mqtt.subscribe(TOPIC_SUBSCRIBLE);
      } else {
        digitalWrite(LED_PILOT_PIN, true);
        Serial.print(".");
        delay(250);
        digitalWrite(LED_PILOT_PIN, false);
        delay(250);
      }
    }
  }
}

void setupRedis() {
  Serial.println("---");
  Serial.print("Connecting Redis ");
  while (!redisConn.connect(REDIS_ADDR, REDIS_PORT)) {
    digitalWrite(LED_PILOT_PIN, true);
    Serial.print(".");
    delay(250);
    digitalWrite(LED_PILOT_PIN, false);
    delay(250);
  }
  auto connStatus = redis.authenticate(REDIS_PASSWORD);
  if (connStatus == RedisSuccess) {
    Serial.println();
    Serial.println("Connected Redis");
    Serial.print("DNS ");
    Serial.println(REDIS_ADDR);
  } else {
    Serial.printf("Failed to authenticate to the Redis server! Errno: %d\n", (int) connStatus);
  }
}

void setupPins() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PILOT_PIN, OUTPUT);
  pinMode(LED_STATUS_PIN, OUTPUT);
}

void reconnect() {
  setupWifi();
  setupmqtt();
}

void callbackmqtt(char* topic, byte* menssage, unsigned int length) {
  Serial.print("Mqtt topic ");
  Serial.println(topic);

  String json;
  for (int i = 0; i < length; i++) {
    char c = (char) menssage[i];
    json += c;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.println("Mqtt menssage is not json");
    return;
  }

  String op = doc["op"];
  Serial.print("Mqtt option ");
  Serial.println(op);
  String value = doc["value"];
  Serial.print("Mqtt button ");
  Serial.println(value);
  nameButton = value;

  if (op.equals("save")) {
    saveButton();
  }

  if (op.equals("click")) {
    clickButton();
  }

  if (op.equals("auto")) {
    boolean state = doc["state"];
    nameState = state;
  }

  Serial.println("---");
}

unsigned long prevMilisStatus = 0, intervalMilisStatus = 600;

void loop() {
  //  reconnect();
  unsigned long currentMillis = millis();
  if (recording) { // IF MODE SAVE
    if (irrecv.decode(&results)) {
      unsigned long irValue = results.value;
      if (irValue == 0xffffffff) {
        irrecv.resume();
        return;
      } else {
        uint16_t *raw_array = resultToRawArray(&results);
        uint16_t lengt = getCorrectedRawLength(&results);
        irrecv.resume();

        bipBUZZER_PIN();
        saveInRedis(raw_array, lengt);
        saveConcluded();
      }
    }
  }
  if (nameState) {
    onOffAuto();
  }
  if (!recording && (currentMillis - prevMilisStatus > intervalMilisStatus)) {
    prevMilisStatus = currentMillis;
    sendStatus();
  }
  mqtt.loop();
}

void bipBUZZER_PIN() {
  for (int x = 0; x < 3; x++) {
    digitalWrite(BUZZER_PIN, true);
    delay(70);
    digitalWrite(BUZZER_PIN, false);
    delay(70);
  }
}

void saveInRedis(uint16_t *raw_array, uint16_t lengt) {
  String raw_data_s = "";
  for (int x = 0; x <= lengt; x++) {
    raw_data_s += raw_array[x];
    if (x != lengt) {
      raw_data_s += ",";
    }
  }
  Serial.print("Redis button: ");
  Serial.println(nameButton);
  Serial.print("Redis Code: ");
  Serial.println(raw_data_s);

  char json[800];
  DynamicJsonDocument doc(1024);
  doc["code"] = raw_data_s.c_str();
  doc["lenght"] = lengt;
  serializeJson(doc, json);

  redis.set(nameButton.c_str(), json);
}

void saveButton() {
  irrecv.enableIRIn();
  digitalWrite(LED_STATUS_PIN, true);
  recording = true;
}

void saveConcluded() {
  irrecv.disableIRIn();
  digitalWrite(LED_STATUS_PIN, false);
  recording = false;
  nameButton = "";
}

void clickButton() {
  String json = redis.get(nameButton.c_str());

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, json);

  Serial.print("Redis button: ");
  Serial.println(nameButton);
  Serial.print("Redis code: ");
  String code = doc["code"];
  Serial.println(code);
  uint16_t lenght = doc["lenght"];
  Serial.print("Redis code lenght: ");
  Serial.println(lenght);
  uint16_t raw_data[lenght];
  covertStringToRawData(code, raw_data, lenght);
  irsend.sendRaw(raw_data, lenght, kFrequency);
  digitalWrite(LED_STATUS_PIN, true);
  delay(250);
  digitalWrite(LED_STATUS_PIN, false);
  irsend.sendRaw(raw_data, lenght, kFrequency);
}

void covertStringToRawData(String str, uint16_t *raw_data, uint16_t lenght) {
  uint16_t count = 0;
  while (str.length() > 0) {
    int index = str.indexOf(',');
    if (index == -1) {
      raw_data[count++] = (uint16_t) atoi(str.c_str());
      break;
    } else {
      raw_data[count++] = (uint16_t) atoi(str.substring(0, index).c_str());
      str = str.substring(index + 1);
    }
  }
}

void sendStatus() {
  float humidty = dht.readHumidity();
  float temperature = dht.readTemperature();
  if (isnan(humidty) || isnan(temperature)) {
    Serial.println("Dht error in read sensor");
  } else {
    char json[1024];
    DynamicJsonDocument doc(1024);
    doc["temp"] = temperature;
    doc["humi"] = humidty;
    serializeJson(doc, json);
    mqtt.publish(TOPIC_PUBLISH_STATUS, json, true);
  }
}

void onOffAuto() {
  float humidty = dht.readHumidity();
  float temperature = dht.readTemperature();
  if (temperature < 22) {
    nameButton = "off";
    if (redis.exists(nameButton.c_str())) {
      clickButton();
      nameState = false;
    }
  } else if (temperature > 26) {
    nameButton = "on";
    if (redis.exists(nameButton.c_str())) {
      clickButton();
      nameState = false;
    }
  } else {
    nameState = true;
  }
}
