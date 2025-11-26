#ifndef WEB_SERVER
#define WEB_SERVER

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>

// Replace with your network credentials
const char* ssid = "Lockheim";
const char* password = "Gvunna123";

// Set LED GPIO
const int ledPin = 25;
// Stores LED state 
String ledState;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Variable moved from pcnt-configuration.h as it is used for JSON state
static volatile int32_t s_last_pcnt_count = 0;

// Forward declarations
String makeStateJson();
String getCounterValue();
String getCounterFrequency();
String processor(const String& var);
void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len);
void sendRealtimeData();

String getCounterValue() {
  // Assumes pcnt_get_total_value() is available (included from pcnt-configuration.h)
  int32_t count = pcnt_get_total_value();
  return String(count);
}
  
String getCounterFrequency() {
  // Assumes s_frequency_hz is available (included from timer-operation.h)
  return String(s_frequency_hz);
}

// Replaces placeholder with LED state value
String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    if(digitalRead(ledPin)){
      ledState = "ON";
    }
    else{
      ledState = "OFF";
    }
    Serial.println(ledState);
    return ledState;
  }
  else if (var == "HSC_COUNT_VALUE"){
    return getCounterValue();
  }
  else if (var == "HSC_FREQUENCY"){
    return getCounterFrequency();
  }
  return String();
}

String makeStateJson() {
  int32_t current = pcnt_get_total_value();
  String state = digitalRead(ledPin) ? "ON" : "OFF";
  // tentukan arah dari delta
  int32_t delta = current - s_last_pcnt_count;
  String dir = "STOP";
  if (delta > 0) dir = "CW";   // putuskan CW / CCW sesuai definisi fisik
  else if (delta < 0) dir = "CCW";
  s_last_pcnt_count = current;

  char buf[128];
  int n = snprintf(buf, sizeof(buf),
                   "{\"count\":%ld,\"freq\":%d,\"state\":\"%s\",\"dir\":\"%s\"}",
                   (long)current, s_frequency_hz, state.c_str(), dir.c_str());
  return String(buf);
}
 
void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS client #%u connected\n", client->id());
    client->setCloseClientOnQueueFull(false);
    // send current state/count/freq to the newly connected client
    String json = makeStateJson();
    client->text(json);
  } 
  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS client #%u disconnected\n", client->id());
  } 
  else if (type == WS_EVT_ERROR) {
    Serial.printf("WS client #%u error\n", client ? client->id() : 0);
  } 
  else if (type == WS_EVT_PONG) {
    // pong received
  } 
  else if (type == WS_EVT_DATA) {
    String msg = "";
    for (size_t i = 0; i < len; i++) msg += (char) data[i];
    Serial.printf("WS message from #%u: %s\n", client->id(), msg.c_str());
  }
}

void sendRealtimeData() {
  String json = makeStateJson();
  ws.textAll(json);
}

void initWebServer() {
  pinMode(ledPin, OUTPUT);

  // Initialize LittleFS
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  // --- START AP MODE ---
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(ssid, password);
  if(!ok){
    Serial.println("Failed to start AP");
  } else {
    Serial.println("AP started");
  }

  // Print ESP32 AP IP Address
  Serial.print("AP Mode IP: ");
  Serial.println(WiFi.softAPIP().toString());
  // --- END AP MODE ---

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, processor);
  });

  // other routes
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  // Turn ON
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, HIGH);
    String json = String("{\"state\":\"ON\"}");
    request->send(200, "application/json", json);
    ws.textAll(makeStateJson());
  });

  // Turn OFF
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    digitalWrite(ledPin, LOW);
    String json = String("{\"state\":\"OFF\"}");
    request->send(200, "application/json", json);
    ws.textAll(makeStateJson());
  });

  // Optional HTTP endpoints
  server.on("/counter", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", getCounterValue().c_str());
  });
  server.on("/frequency", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", getCounterFrequency().c_str());
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
}

#endif // WEB_SERVER   