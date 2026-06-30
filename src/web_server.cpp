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
#include "CT_timer.h"
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

extern CT_timer* timer1;
extern CT_timer* timer2;

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
    
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{"
             "\"ch1_func\":%d,\"ch1_count\":%ld,\"ch1_freq\":%ld,\"ch1_mode\":\"%s\",\"ch1_out\":%d,\"ch1_preset\":%ld,"
             "\"ch1_t_curr\":%lu,\"ch1_t_pre\":%lu,"
             "\"ch2_func\":%d,\"ch2_count\":%ld,\"ch2_freq\":%ld,\"ch2_mode\":\"%s\",\"ch2_out\":%d,\"ch2_preset\":%ld,"
             "\"ch2_t_curr\":%lu,\"ch2_t_pre\":%lu,"
             "\"uptime\":%lu,\"heap\":%lu"
             "}",
             (int)ch1_function,
             (long)pcnt_ch1_get_count(),
             (long)s_ch1_frequency_hz,
             input_mode_to_string(cfg1.input_mode),
             (ch1_function == C_FUNCTION_COUNTER) ? (ctr1 ? (ctr1->getOutputState() ? 1 : 0) : 0) : (timer1 ? (timer1->getOutputState() ? 1 : 0) : 0),
             ctr1 ? (long)ctr1->getPresetValue() : 10000L,
             timer1 ? (unsigned long)timer1->getCurrentTime() : 0UL,
             timer1 ? (unsigned long)timer1->getPresetTime() : 0UL,
             (int)ch2_function,
             (long)pcnt_ch2_get_count(),
             (long)s_ch2_frequency_hz,
             input_mode_to_string(cfg2.input_mode),
             (ch2_function == C_FUNCTION_COUNTER) ? (ctr2 ? (ctr2->getOutputState() ? 1 : 0) : 0) : (timer2 ? (timer2->getOutputState() ? 1 : 0) : 0),
             ctr2 ? (long)ctr2->getPresetValue() : 10000L,
             timer2 ? (unsigned long)timer2->getCurrentTime() : 0UL,
             timer2 ? (unsigned long)timer2->getPresetTime() : 0UL,
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
        request->send(LittleFS, "/dashboard.html", String(), false, processor);
    });

    server.on("/dashboard.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/dashboard.html", String(), false, processor);
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/configuration.html", String(), false, processor);
    });

    server.on("/configuration.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/configuration.html", String(), false, processor);
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
        request->send(200, "application/json", "{\"status\":\"disabled\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/timer/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
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
        
        StoredConfig_t nvsConfig;
        nvs_config_load(&nvsConfig);
        
        const char* edge_str[] = {"RISING", "FALLING", "BOTH"};
        
        const char* ch1_out_str = "OUTPUT_N";
        if(nvsConfig.ch1_output_mode == OUTPUT_F) ch1_out_str = "OUTPUT_F";
        else if(nvsConfig.ch1_output_mode == OUTPUT_C) ch1_out_str = "OUTPUT_C";
        else if(nvsConfig.ch1_output_mode == OUTPUT_NONE) ch1_out_str = "OUTPUT_NONE";

        const char* ch2_out_str = "OUTPUT_N";
        if(nvsConfig.ch2_output_mode == OUTPUT_F) ch2_out_str = "OUTPUT_F";
        else if(nvsConfig.ch2_output_mode == OUTPUT_C) ch2_out_str = "OUTPUT_C";
        else if(nvsConfig.ch2_output_mode == OUTPUT_NONE) ch2_out_str = "OUTPUT_NONE";

        char buf[1024];
        snprintf(buf, sizeof(buf),
            "{"
            "\"ch1_mode\":\"%s\",\"ch1_preset\":%ld,\"ch1_edge\":\"%s\",\"ch1_out\":\"%s\","
            "\"ch1_func\":%d,\"ch1_t_mode\":\"%s\",\"ch1_t_pre\":%u,"
            "\"ch2_mode\":\"%s\",\"ch2_preset\":%ld,\"ch2_edge\":\"%s\",\"ch2_out\":\"%s\","
            "\"ch2_func\":%d,\"ch2_t_mode\":\"%s\",\"ch2_t_pre\":%u,"
            "\"mb_addr\":%u,\"mb_baud\":%u"
            "}",
            input_mode_to_string(cfg1.input_mode),
            ctr1 ? (long)ctr1->getPresetValue() : 10000L,
            edge_str[cfg1.edge_mode],
            ch1_out_str,
            (int)nvsConfig.ch1_function,
            nvsConfig.ch1_timer_mode == TIMER_FLK ? "FLK" : "OND",
            nvsConfig.ch1_timer_preset,
            input_mode_to_string(cfg2.input_mode),
            ctr2 ? (long)ctr2->getPresetValue() : 10000L,
            edge_str[cfg2.edge_mode],
            ch2_out_str,
            (int)nvsConfig.ch2_function,
            nvsConfig.ch2_timer_mode == TIMER_FLK ? "FLK" : "OND",
            nvsConfig.ch2_timer_preset,
            nvsConfig.modbus_address,
            nvsConfig.modbus_baud);
        
        request->send(200, "application/json", buf);
    });
    
    // Configure Channel 1
    server.on("/config/ch1", HTTP_GET, [](AsyncWebServerRequest *request) {
        String result = "{\"status\":\"ok\"}";
        StoredConfig_t nvsConfig;
        nvs_config_load(&nvsConfig);
        
        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            InputMode im = MODE_UP;
            if (mode == "DN") im = MODE_DN;
            else if (mode == "UDA") im = MODE_UDA;
            else if (mode == "UDC") im = MODE_UDC;
            
            ChannelInputConfig_t cfg = input_config_get(0);
            cfg.input_mode = im;
            input_config_set_mode(0, &cfg);
            nvsConfig.ch1_input_mode = im;
        }
        
        if (request->hasParam("out")) {
            String out = request->getParam("out")->value();
            OutputMode om = OUTPUT_N;
            if (out == "OUTPUT_F") om = OUTPUT_F;
            else if (out == "OUTPUT_C") om = OUTPUT_C;
            else if (out == "OUTPUT_NONE") om = OUTPUT_NONE;
            
            CT_counter* ctr = getCounterInstance(1);
            if (ctr) ctr->setOutputMode(om);
            nvsConfig.ch1_output_mode = om;
        }
        
        if (request->hasParam("edge")) {
            String edge = request->getParam("edge")->value();
            EdgeMode em = EDGE_RISING;
            if (edge == "FALLING") em = EDGE_FALLING;
            else if (edge == "BOTH") em = EDGE_BOTH;
            
            ChannelInputConfig_t cfg = input_config_get(0);
            cfg.edge_mode = em;
            input_config_set_mode(0, &cfg);
            nvsConfig.ch1_edge_mode = em;
        }
        
        if (request->hasParam("preset")) {
            int32_t preset = request->getParam("preset")->value().toInt();
            CT_counter* ctr = getCounterInstance(1);
            if (ctr) {
                ctr->setPresetValue(preset);
            }
            nvsConfig.ch1_preset_value = preset;
        }
        
        nvs_config_save(&nvsConfig);
        request->send(200, "application/json", result);
        ws.textAll(make_state_json());
    });
    
    // Configure Channel 2
    server.on("/config/ch2", HTTP_GET, [](AsyncWebServerRequest *request) {
        String result = "{\"status\":\"ok\"}";
        StoredConfig_t nvsConfig;
        nvs_config_load(&nvsConfig);
        
        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            InputMode im = MODE_UP;
            if (mode == "DN") im = MODE_DN;
            else if (mode == "UDA") im = MODE_UDA;
            else if (mode == "UDC") im = MODE_UDC;
            
            ChannelInputConfig_t cfg = input_config_get(1);
            cfg.input_mode = im;
            input_config_set_mode(1, &cfg);
            nvsConfig.ch2_input_mode = im;
        }

        if (request->hasParam("out")) {
            String out = request->getParam("out")->value();
            OutputMode om = OUTPUT_N;
            if (out == "OUTPUT_F") om = OUTPUT_F;
            else if (out == "OUTPUT_C") om = OUTPUT_C;
            else if (out == "OUTPUT_NONE") om = OUTPUT_NONE;
            
            CT_counter* ctr = getCounterInstance(2);
            if (ctr) ctr->setOutputMode(om);
            nvsConfig.ch2_output_mode = om;
        }
        
        if (request->hasParam("edge")) {
            String edge = request->getParam("edge")->value();
            EdgeMode em = EDGE_RISING;
            if (edge == "FALLING") em = EDGE_FALLING;
            else if (edge == "BOTH") em = EDGE_BOTH;
            
            ChannelInputConfig_t cfg = input_config_get(1);
            cfg.edge_mode = em;
            input_config_set_mode(1, &cfg);
            nvsConfig.ch2_edge_mode = em;
        }
        
        if (request->hasParam("preset")) {
            int32_t preset = request->getParam("preset")->value().toInt();
            CT_counter* ctr = getCounterInstance(2);
            if (ctr) {
                ctr->setPresetValue(preset);
            }
            nvsConfig.ch2_preset_value = preset;
        }
        
        nvs_config_save(&nvsConfig);
        request->send(200, "application/json", result);
        ws.textAll(make_state_json());
    });
    
    // Configure Timer 1
    server.on("/config/timer1", HTTP_GET, [](AsyncWebServerRequest *request) {
        StoredConfig_t nvsConfig;
        nvs_config_load(&nvsConfig);

        if (request->hasParam("mode")) {
            String m = request->getParam("mode")->value();
            TimerOutputMode tm = (m == "FLK" ? TIMER_FLK : TIMER_OND);
            if (timer1) timer1->setOutputMode(tm);
            nvsConfig.ch1_timer_mode = tm;
        }
        if (request->hasParam("preset")) {
            uint32_t ms = request->getParam("preset")->value().toInt();
            if (timer1) timer1->setPresetTime(ms);
            nvsConfig.ch1_timer_preset = ms;
        }

        nvs_config_save(&nvsConfig);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });

    // Configure Timer 2
    server.on("/config/timer2", HTTP_GET, [](AsyncWebServerRequest *request) {
        StoredConfig_t nvsConfig;
        nvs_config_load(&nvsConfig);

        if (request->hasParam("mode")) {
            String m = request->getParam("mode")->value();
            TimerOutputMode tm = (m == "FLK" ? TIMER_FLK : TIMER_OND);
            if (timer2) timer2->setOutputMode(tm);
            nvsConfig.ch2_timer_mode = tm;
        }
        if (request->hasParam("preset")) {
            uint32_t ms = request->getParam("preset")->value().toInt();
            if (timer2) timer2->setPresetTime(ms);
            nvsConfig.ch2_timer_preset = ms;
        }

        nvs_config_save(&nvsConfig);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });
    
    // Configure Modbus
    server.on("/config/modbus", HTTP_GET, [](AsyncWebServerRequest *request) {
        StoredConfig_t nvsConfig;
        nvs_config_load(&nvsConfig);
        
        if (request->hasParam("addr")) {
            nvsConfig.modbus_address = request->getParam("addr")->value().toInt();
        }
        if (request->hasParam("baud")) {
            nvsConfig.modbus_baud = request->getParam("baud")->value().toInt();
        }
        
        nvs_config_save(&nvsConfig);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // Save configuration to NVS
    server.on("/config/save", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Now mostly handled per endpoint, but we keep this to acknowledge manual save request
        request->send(200, "application/json", "{\"status\":\"saved\"}");
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
    
    // Channel mode toggles
    server.on("/config/ch1/function", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("val")) {
            int val = request->getParam("val")->value().toInt();
            ch1_function = (val == 1) ? C_FUNCTION_TIMER : C_FUNCTION_COUNTER;
            
            StoredConfig_t nvsConfig;
            nvs_config_load(&nvsConfig);
            nvsConfig.ch1_function = ch1_function;
            nvs_config_save(&nvsConfig);
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        ws.textAll(make_state_json());
    });
    
    server.on("/config/ch2/function", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("val")) {
            int val = request->getParam("val")->value().toInt();
            ch2_function = (val == 1) ? C_FUNCTION_TIMER : C_FUNCTION_COUNTER;
            
            StoredConfig_t nvsConfig;
            nvs_config_load(&nvsConfig);
            nvsConfig.ch2_function = ch2_function;
            nvs_config_save(&nvsConfig);
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
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
