#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ETH.h>
#include <WiFiServer.h>

// globals for ethernet connection
static bool eth_connected = false;
WiFiServer server(80);
IPAddress local_ip(10, 200, 10, 35);
IPAddress gateway(10, 200, 10, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(10, 200, 10, 1);
IPAddress dns2(1, 1, 1, 1);

// globals for motor control
const int IN3 = 13;
const int IN4 = 16;
const int ENA = 4;

// globals for switches
const int SW_UR = 15;
const int SW_LR = 5;
const int SW_UL = 2;
const int SW_LL = 14;

void ArduinoEvent(WiFiEvent_t);
void SetSpeed(int);
void HandlePost(char *);
void Brake();
void Spin(int);

template <typename T> int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

// global variables
int CurrSpeed = 0;
char request[1000];
char html[1000];

void setup() {
  Serial.begin(115200);
  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "flash";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed");
      });

  delay(250);
  WiFi.onEvent(ArduinoEvent);
  ETH.begin();
  pinMode(IN4, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(ENA, OUTPUT);

  pinMode(SW_UR, INPUT_PULLUP);
  pinMode(SW_LR, INPUT_PULLUP);
  pinMode(SW_UL, INPUT_PULLUP);
  pinMode(SW_LL, INPUT_PULLUP);

  SetSpeed(0);
}

bool startsWith(const char *pre, const char *str) {
  return strncmp(pre, str, strlen(pre)) == 0;
}

size_t readLineFromClient(WiFiClient *client, char *buffer, size_t length) {
  size_t i = client->readBytesUntil('\r', buffer, length);
  client->readBytes(buffer + i, 1);
  buffer[i] = '\0';
  return i;
}

void loop() {
  ArduinoOTA.handle();
  if (server.hasClient()) {
    WiFiClient client = server.available();
    if (client.connected()) {
      readLineFromClient(&client, request, sizeof(request));
      if (startsWith("GET", request)) {
        int len = sprintf(html,
                          "<html><head><title>ESP32 Web Server</title></head><body><h1>ESP32 Web Server</h1>"
                          "<div>Upper Right: %d</div>"
                          "<div>Lower Right: %d</div>"
                          "<div>Upper Left: %d</div>"
                          "<div>Lower Left: %d</div>"
                          "<div>Speed: %d</div>"
                          "</body></html>",
                          digitalRead(SW_UR), digitalRead(SW_LR), digitalRead(SW_UL), digitalRead(SW_LL), CurrSpeed);
        // send basic html file to client
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Content-Length: " + String(len));
        client.println("Connection: Closed");
        client.println();
        client.print(html);
        client.flush();
      } else if (startsWith("POST", request)) {
        int contentLength = 0;
        while (readLineFromClient(&client, request, sizeof(request)) > 0) {
          if (startsWith("Content-Length:", request))
            contentLength = atoi(request + 16);
        }
        for (int i = 0; i < contentLength; i++)
          request[i] = (uint8_t)client.read();
        request[contentLength] = '\0';
        HandlePost(request);
        String html = "received POST request " + String(request);
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Content-Length: " + String(html.length()));
        client.println("Connection: Closed");
        client.println();
        client.print(html);
        client.flush();
      }
      client.stop();
    }
  }
  delay(100);
}

void ArduinoEvent(arduino_event_id_t event) {
  switch (event) {
  case ARDUINO_EVENT_ETH_START:
    Serial.println("ETH Started");
    ETH.setHostname("tv-controller");
    ETH.config(local_ip, gateway, subnet, dns1, dns2);
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.println("ETH Connected");
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    Serial.print("ETH MAC: ");
    Serial.print(ETH.macAddress());
    Serial.print(", IPv4: ");
    Serial.print(ETH.localIP());
    if (ETH.fullDuplex()) {
      Serial.print(", FULL_DUPLEX");
    }
    Serial.print(", ");
    Serial.print(ETH.linkSpeed());
    Serial.println("Mbps");
    ArduinoOTA.begin();
    server.begin();
    eth_connected = true;
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED:
    Serial.println("ETH Disconnected");
    eth_connected = false;
    break;
  case ARDUINO_EVENT_ETH_STOP:
    Serial.println("ETH Stopped");
    eth_connected = false;
    break;
  default:
    break;
  }
}

void Spin(int Speed) {
  if (Speed == 0)
    Brake();
  else {
    digitalWrite(IN3, Speed < 0 ? LOW : HIGH);
    digitalWrite(IN4, Speed < 0 ? HIGH : LOW);
    analogWrite(ENA, abs(Speed));
  }
}

void Brake() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void SetSpeed(int TargetSpeed) {
  if (TargetSpeed > 255)
    TargetSpeed = 255;
  else if (TargetSpeed < -255)
    TargetSpeed = -255;
  int diff = TargetSpeed - CurrSpeed;
  // Serial.print(CurrSpeed);
  // Serial.print(" --> ");
  // Serial.println(TargetSpeed);
  int stepSize = sgn(diff);
  Serial.println(stepSize);
  while (CurrSpeed != TargetSpeed) {
    CurrSpeed += stepSize;
    Spin(round(CurrSpeed));
    Serial.println(CurrSpeed);
    delay(4);
  }
}

void HandlePost(char *buff) {
  if (startsWith("speed", buff)) {
    // int speed, num = sscanf(buff, "speed %d", &speed);
    // if (num == 1)
    //   SetSpeed(speed);
  } else if (startsWith("up", buff)) {
    for(int speed = 0; speed < 255 && digitalRead(SW_UR) == LOW && digitalRead(SW_UL) == LOW; speed+=5)
      SetSpeed(speed);
    while (digitalRead(SW_UR) == LOW && digitalRead(SW_UL) == LOW)
      delay(10);
    SetSpeed(0);
  } else if (startsWith("down", buff)) {
    for(int speed = 0; speed > -255 && digitalRead(SW_LR) == LOW && digitalRead(SW_LL) == LOW; speed-=5)
      SetSpeed(speed);
    while (digitalRead(SW_LR) == LOW && digitalRead(SW_LL) == LOW)
      delay(10);
    SetSpeed(0);
  } else if (startsWith("stop", buff)) {
    Spin(0);
  }
}