/**
 * @file web_server.cpp
 * @brief AsyncWebServer and WebSocket implementation
 */

#include "web_server.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "timer_operation.h"
#include "CT_counter.h"
#include "input_config.h"
#include "nvs_config.h"
#include "rtos_tasks.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <LittleFS.h>

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================

AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket ws(WS_PATH);

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

String get_counter_value() {
    int32_t count = pcnt_ch1_get_count();
    return String(count);
}

String get_counter_frequency() {
    return String(s_ch1_frequency_hz);
}

/**
 * @brief Template processor for HTML files
 */
static String processor(const String& var) {
    if (var == "HSC_COUNT_VALUE") {
        return get_counter_value();
    }
    else if (var == "HSC_FREQUENCY") {
        return get_counter_frequency();
    }
    return String();
}

/**
 * @brief Generate JSON state string for WebSocket
 */
static String make_state_json() {
    static uint32_t uptime_sec = 0;
    uptime_sec = millis() / 1000;
    
    // Get counter instances
    CT_counter* ctr1 = getCounterInstance(1);
    CT_counter* ctr2 = getCounterInstance(2);
    
    // Get input configs
    ChannelInputConfig_t cfg1 = input_config_get(0);
    ChannelInputConfig_t cfg2 = input_config_get(1);
    
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{"
             "\"ch1_count\":%ld,\"ch1_freq\":%ld,\"ch1_mode\":\"%s\",\"ch1_out\":%d,\"ch1_preset\":%ld,"
             "\"ch2_count\":%ld,\"ch2_freq\":%ld,\"ch2_mode\":\"%s\",\"ch2_out\":%d,\"ch2_preset\":%ld,"
             "\"timer\":%lu,\"uptime\":%lu,\"heap\":%lu"
             "}",
             (long)pcnt_ch1_get_count(),
             (long)s_ch1_frequency_hz,
             input_mode_to_string(cfg1.input_mode),
             ctr1 ? (ctr1->getOutputState() ? 1 : 0) : 0,
             ctr1 ? (long)ctr1->getPresetValue() : 10000L,
             (long)pcnt_ch2_get_count(),
             (long)s_ch2_frequency_hz,
             input_mode_to_string(cfg2.input_mode),
             ctr2 ? (ctr2->getOutputState() ? 1 : 0) : 0,
             ctr2 ? (long)ctr2->getPresetValue() : 10000L,
             (unsigned long)elapsed_ms,
             (unsigned long)uptime_sec,
             (unsigned long)ESP.getFreeHeap());
    
    return String(buf);
}

// =============================================================================
// WEBSOCKET EVENT HANDLER
// =============================================================================

static void onWsEvent(AsyncWebSocket *server,
                      AsyncWebSocketClient *client,
                      AwsEventType type,
                      void *arg,
                      uint8_t *data,
                      size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WS client #%u connected\n", client->id());
            client->setCloseClientOnQueueFull(false);
            // Send current state to newly connected client
            client->text(make_state_json());
            break;
            
        case WS_EVT_DISCONNECT:
            Serial.printf("WS client #%u disconnected\n", client->id());
            break;
            
        case WS_EVT_ERROR:
            Serial.printf("WS client #%u error\n", client ? client->id() : 0);
            break;
            
        case WS_EVT_PONG:
            // Pong received
            break;
            
        case WS_EVT_DATA: {
            // Process received message
            String msg = "";
            for (size_t i = 0; i < len; i++) {
                msg += (char)data[i];
            }
            Serial.printf("WS message from #%u: %s\n", client->id(), msg.c_str());
            client->text("ACK");
            break;
        }
    }
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

void sendRealtimeData() {
    String json = make_state_json();
    ws.textAll(json);
}

void web_server_init() {
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("ERROR: Failed to mount LittleFS");
        return;
    }
    Serial.println("LittleFS mounted");
    
    // Start WiFi Access Point
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    if (!ok) {
        Serial.println("ERROR: Failed to start WiFi AP");
        return;
    }
    
    Serial.printf("WiFi AP started: %s\n", WIFI_AP_SSID);
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    
    // =========================================================================
    // STATIC FILE ROUTES
    // =========================================================================
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", String(), false, processor);
    });
    
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/style.css", "text/css");
    });
    
    // =========================================================================
    // COUNTER API ENDPOINTS
    // =========================================================================
    
    server.on("/counter/enable", HTTP_GET, [](AsyncWebServerRequest *request) {
        pcnt_resume();
        CT_counter* ctr1 = getCounterInstance(1);
        if (ctr1) ctr1->enable();
        CT_counter* ctr2 = getCounterInstance(2);
        if (ctr2) ctr2->enable();
        continuous_mode = true;
        counter_enabled = true;
        request->send(200, "application/json", "{\"status\":\"enabled\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/counter/disable", HTTP_GET, [](AsyncWebServerRequest *request) {
        pcnt_pause();
        CT_counter* ctr1 = getCounterInstance(1);
        if (ctr1) ctr1->disable();
        CT_counter* ctr2 = getCounterInstance(2);
        if (ctr2) ctr2->disable();
        continuous_mode = false;
        counter_enabled = false;
        request->send(200, "application/json", "{\"status\":\"disabled\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/counter/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        pcnt_reset();
        s_ch1_last_count = 0;
        s_ch1_frequency_hz = 0;
        request->send(200, "application/json", "{\"status\":\"reset\"}");
        ws.textAll(make_state_json());
    });

    auto handle_manual_output = [](AsyncWebServerRequest *request, uint8_t channel, const char *state) {
        CT_counter* ctr = getCounterInstance(channel);
        if (ctr) {
            if (strcmp(state, "auto") == 0) {
                ctr->clearManualOutputOverride();
            } else {
                ctr->setManualOutputState(strcmp(state, "on") == 0);
            }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    };
     
    // Manual counter controls
    server.on("/counter/manual/1/plus", HTTP_GET, [](AsyncWebServerRequest *request) {
        CT_counter* ctr = getCounterInstance(1);
        if (ctr) ctr->manualIncrement();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });

    server.on("/counter/manual/1/minus", HTTP_GET, [](AsyncWebServerRequest *request) {
        CT_counter* ctr = getCounterInstance(1);
        if (ctr) ctr->manualDecrement();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });

    server.on("/counter/manual/2/plus", HTTP_GET, [](AsyncWebServerRequest *request) {
        CT_counter* ctr = getCounterInstance(2);
        if (ctr) ctr->manualIncrement();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });

    server.on("/counter/manual/2/minus", HTTP_GET, [](AsyncWebServerRequest *request) {
        CT_counter* ctr = getCounterInstance(2);
        if (ctr) ctr->manualDecrement();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });

    // Manual relay/output controls for testing
    server.on("/output/1/on", HTTP_GET, [handle_manual_output](AsyncWebServerRequest *request) {
        handle_manual_output(request, 1, "on");
    });
    server.on("/output/1/off", HTTP_GET, [handle_manual_output](AsyncWebServerRequest *request) {
        handle_manual_output(request, 1, "off");
    });
    server.on("/output/1/auto", HTTP_GET, [handle_manual_output](AsyncWebServerRequest *request) {
        handle_manual_output(request, 1, "auto");
    });

    server.on("/output/2/on", HTTP_GET, [handle_manual_output](AsyncWebServerRequest *request) {
        handle_manual_output(request, 2, "on");
    });
    server.on("/output/2/off", HTTP_GET, [handle_manual_output](AsyncWebServerRequest *request) {
        handle_manual_output(request, 2, "off");
    });
    server.on("/output/2/auto", HTTP_GET, [handle_manual_output](AsyncWebServerRequest *request) {
        handle_manual_output(request, 2, "auto");
    });
    
    // =========================================================================
    // TIMER API ENDPOINTS
    // =========================================================================
    
    server.on("/timer/enable", HTTP_GET, [](AsyncWebServerRequest *request) {
        timer_enabled = true;
        request->send(200, "application/json", "{\"status\":\"enabled\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/timer/disable", HTTP_GET, [](AsyncWebServerRequest *request) {
        timer_enabled = false;
        stopwatch_pause();
        stopwatch_running = false;
        request->send(200, "application/json", "{\"status\":\"disabled\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/timer/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        stopwatch_reset();
        elapsed_ms = 0;
        request->send(200, "application/json", "{\"status\":\"reset\"}");
        ws.textAll(make_state_json());
    });
    
    // =========================================================================
    // DEBUG ENDPOINTS
    // =========================================================================
    
    server.on("/counter", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", get_counter_value().c_str());
    });
    
    server.on("/frequency", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", get_counter_frequency().c_str());
    });
    
    // =========================================================================
    // CONFIGURATION API ENDPOINTS
    // =========================================================================
    
    // Get current configuration
    server.on("/config/get", HTTP_GET, [](AsyncWebServerRequest *request) {
        ChannelInputConfig_t cfg1 = input_config_get(0);
        ChannelInputConfig_t cfg2 = input_config_get(1);
        CT_counter* ctr1 = getCounterInstance(1);
        CT_counter* ctr2 = getCounterInstance(2);
        
        const char* edge_str[] = {"RISING", "FALLING", "BOTH"};
        
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{"
            "\"ch1_mode\":\"%s\",\"ch1_preset\":%ld,\"ch1_edge\":\"%s\","
            "\"ch2_mode\":\"%s\",\"ch2_preset\":%ld,\"ch2_edge\":\"%s\""
            "}",
            input_mode_to_string(cfg1.input_mode),
            ctr1 ? (long)ctr1->getPresetValue() : 10000L,
            edge_str[cfg1.edge_mode],
            input_mode_to_string(cfg2.input_mode),
            ctr2 ? (long)ctr2->getPresetValue() : 10000L,
            edge_str[cfg2.edge_mode]);
        
        request->send(200, "application/json", buf);
    });
    
    // Configure Channel 1
    server.on("/config/ch1", HTTP_GET, [](AsyncWebServerRequest *request) {
        String result = "{\"status\":\"ok\"}";
        
        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            InputMode im = MODE_UP;
            if (mode == "DN") im = MODE_DN;
            else if (mode == "UDA") im = MODE_UDA;
            else if (mode == "UDC") im = MODE_UDC;
            
            ChannelInputConfig_t cfg = input_config_get(0);
            cfg.input_mode = im;
            input_config_set_mode(0, &cfg);
            nvs_save_input_mode(0, im);
        }
        
        if (request->hasParam("edge")) {
            String edge = request->getParam("edge")->value();
            EdgeMode em = EDGE_RISING;
            if (edge == "FALLING") em = EDGE_FALLING;
            else if (edge == "BOTH") em = EDGE_BOTH;
            
            ChannelInputConfig_t cfg = input_config_get(0);
            cfg.edge_mode = em;
            input_config_set_mode(0, &cfg);
            nvs_save_edge_mode(0, em);
        }
        
        if (request->hasParam("preset")) {
            int32_t preset = request->getParam("preset")->value().toInt();
            CT_counter* ctr = getCounterInstance(1);
            if (ctr) {
                ctr->setPresetValue(preset);
            }
        }
        
        request->send(200, "application/json", result);
        ws.textAll(make_state_json());
    });
    
    // Configure Channel 2
    server.on("/config/ch2", HTTP_GET, [](AsyncWebServerRequest *request) {
        String result = "{\"status\":\"ok\"}";
        
        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            InputMode im = MODE_UP;
            if (mode == "DN") im = MODE_DN;
            else if (mode == "UDA") im = MODE_UDA;
            else if (mode == "UDC") im = MODE_UDC;
            
            ChannelInputConfig_t cfg = input_config_get(1);
            cfg.input_mode = im;
            input_config_set_mode(1, &cfg);
            nvs_save_input_mode(1, im);
        }
        
        if (request->hasParam("edge")) {
            String edge = request->getParam("edge")->value();
            EdgeMode em = EDGE_RISING;
            if (edge == "FALLING") em = EDGE_FALLING;
            else if (edge == "BOTH") em = EDGE_BOTH;
            
            ChannelInputConfig_t cfg = input_config_get(1);
            cfg.edge_mode = em;
            input_config_set_mode(1, &cfg);
            nvs_save_edge_mode(1, em);
        }
        
        if (request->hasParam("preset")) {
            int32_t preset = request->getParam("preset")->value().toInt();
            CT_counter* ctr = getCounterInstance(2);
            if (ctr) {
                ctr->setPresetValue(preset);
            }
        }
        
        request->send(200, "application/json", result);
        ws.textAll(make_state_json());
    });
    
    // Save configuration to NVS
    server.on("/config/save", HTTP_GET, [](AsyncWebServerRequest *request) {
        StoredConfig_t cfg;
        ChannelInputConfig_t ch1 = input_config_get(0);
        ChannelInputConfig_t ch2 = input_config_get(1);
        CT_counter* ctr1 = getCounterInstance(1);
        CT_counter* ctr2 = getCounterInstance(2);
        
        cfg.ch1_input_mode = ch1.input_mode;
        cfg.ch1_edge_mode = ch1.edge_mode;
        cfg.ch1_filter = ch1.filter_value;
        cfg.ch1_preset_value = ctr1 ? ctr1->getPresetValue() : 10000;
        
        cfg.ch2_input_mode = ch2.input_mode;
        cfg.ch2_edge_mode = ch2.edge_mode;
        cfg.ch2_filter = ch2.filter_value;
        cfg.ch2_preset_value = ctr2 ? ctr2->getPresetValue() : 10000;
        
        cfg.modbus_address = MODBUS_SLAVE_ID;
        cfg.modbus_baud = MODBUS_BAUD_RATE;
        cfg.config_version = CONFIG_VERSION;
        
        bool ok = nvs_config_save(&cfg);
        String result = ok ? "{\"status\":\"saved\"}" : "{\"status\":\"error\"}";
        request->send(200, "application/json", result);
    });
    
    // Factory reset
    server.on("/config/factory", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool ok = nvs_config_reset();
        if (ok) {
            StoredConfig_t cfg;
            nvs_config_get_defaults(&cfg);
            nvs_config_apply(&cfg);
            
            // Reset counter instances
            CT_counter* ctr1 = getCounterInstance(1);
            CT_counter* ctr2 = getCounterInstance(2);
            if (ctr1) {
                ctr1->setPresetValue(cfg.ch1_preset_value);
                ctr1->reset();
            }
            if (ctr2) {
                ctr2->setPresetValue(cfg.ch2_preset_value);
                ctr2->reset();
            }
        }
        
        String result = ok ? "{\"status\":\"factory reset complete\"}" : "{\"status\":\"error\"}";
        request->send(200, "application/json", result);
        ws.textAll(make_state_json());
    });
    
    // Reset individual channels
    server.on("/counter/reset/1", HTTP_GET, [](AsyncWebServerRequest *request) {
        CT_counter* ctr = getCounterInstance(1);
        if (ctr) ctr->reset();
        request->send(200, "application/json", "{\"status\":\"ch1 reset\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/counter/reset/2", HTTP_GET, [](AsyncWebServerRequest *request) {
        CT_counter* ctr = getCounterInstance(2);
        if (ctr) ctr->reset();
        request->send(200, "application/json", "{\"status\":\"ch2 reset\"}");
        ws.textAll(make_state_json());
    });
    
    // =========================================================================
    // WEBSOCKET SETUP
    // =========================================================================
    
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    
    // Start server
    server.begin();
    Serial.printf("Web server started on port %d\n", WEB_SERVER_PORT);
}
