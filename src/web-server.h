#ifndef WEB_SERVER
#define WEB_SERVER

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"

// Replace with your network credentials
const char* ssid = "HSC_TIMER_AP";
const char* password = "PENSJOSS";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String get_counter_value() {
  int32_t count = pcnt_get_total_value();
  return String(count);
}
  
String get_counter_frequency() {
  return String(s_frequency_hz_ch1);
}

// Replaces placeholder with LED state value
String processor(const String& var){
  // Serial.println(var);
  if (var == "HSC_COUNT_VALUE"){
    return get_counter_value();
  }
  else if (var == "HSC_FREQUENCY"){
    return get_counter_frequency();
  }
  return String();
}

String make_state_json() {
  static int32_t last_count = 0;
  int32_t current = pcnt_get_total_value();
  
  // tentukan arah dari delta
  int32_t delta = current - last_count;
  String dir = "STOP";
  if (delta > 0) dir = "CW";   // putuskan CW / CCW sesuai definisi fisik
  else if (delta < 0) dir = "CCW";
  last_count = current;
  char buf[256];
  int n = snprintf(buf, sizeof(buf),
                   "{\"count\":%ld,\"freq\":%d,\"dir\":\"%s\",\"timer\":%lu,\"c_en\":%d,\"t_en\":%d}",
                   (long)current, s_frequency_hz_ch1, dir.c_str(), (unsigned long)elapsed_ms, counter_enabled, timer_enabled);
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
    String json = make_state_json();
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
    // If client sent something, data points to bytes (len bytes)
    // data may be fragmented; library assembles and gives you frames.
    // For simple text messages:
    String msg = "";
    for (size_t i = 0; i < len; i++) msg += (char) data[i];
    Serial.printf("WS message from #%u: %s\n", client->id(), msg.c_str());
    // Optionally reply:
    client->text("ACK");
  }
}

void sendRealtimeData() {
  String json = make_state_json();
  ws.textAll(json);
}

void web_server_init() {
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

  // Counter controls
  server.on("/counter/enable", HTTP_GET, [](AsyncWebServerRequest *request){
    pcnt_resume();
    continuous_mode = true;
    counter_enabled = true;
    request->send(200, "application/json", "{\"status\":\"enabled\"}");
    ws.textAll(make_state_json());
  });
  server.on("/counter/disable", HTTP_GET, [](AsyncWebServerRequest *request){
    pcnt_pause();
    continuous_mode = false;
    counter_enabled = false;
    request->send(200, "application/json", "{\"status\":\"disabled\"}");
    ws.textAll(make_state_json());
  });
  server.on("/counter/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    pcnt_reset();
    s_last_count_ch1 = 0;
    s_frequency_hz_ch1 = 0;
    request->send(200, "application/json", "{\"status\":\"reset\"}");
    ws.textAll(make_state_json());
  });

  // Timer controls
  server.on("/timer/enable", HTTP_GET, [](AsyncWebServerRequest *request){
    timer_enabled = true;
    request->send(200, "application/json", "{\"status\":\"enabled\"}");
    ws.textAll(make_state_json());
  });
  server.on("/timer/disable", HTTP_GET, [](AsyncWebServerRequest *request){
    timer_enabled = false;
    stopwatch_pause();
    stopwatch_running = false;
    request->send(200, "application/json", "{\"status\":\"disabled\"}");
    ws.textAll(make_state_json());
  });
  server.on("/timer/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    stopwatch_reset();
    elapsed_ms = 0;
    request->send(200, "application/json", "{\"status\":\"reset\"}");
    ws.textAll(make_state_json());
  });

  // Optional HTTP endpoints (useful during debugging)
  server.on("/counter", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", get_counter_value().c_str());
  });
  server.on("/frequency", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", get_counter_frequency().c_str());
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
}

#endif // WEB_SERVER   