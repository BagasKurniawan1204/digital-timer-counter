/**
 * @file serial_handler.cpp
 * @brief Serial command interface implementation
 */

#include "serial_handler.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "timer_operation.h"
#include "rtos_tasks.h"
#include "CT_counter.h"
#include "input_config.h"
#include "nvs_config.h"
#include "driver/timer.h"

// =============================================================================
// COMMAND PROCESSING
// =============================================================================

void serial_handler_loop() {
    // Process serial commands
    if (Serial.available()) {
        String input = Serial.readString();
        input.trim();
        input.toUpperCase();
        
        if (input == "RESET") {
            pcnt_reset();
            s_ch1_last_count = 0;
            s_ch1_frequency_hz = 0;
            s_ch2_last_count = 0;
            s_ch2_frequency_hz = 0;
            Serial.println("Counter reset complete.");
        }
        else if (input == "START" || input == "CONTINUOUS") {
            continuous_mode = true;
            counter_enabled = true;
            pcnt_resume();
            
            // Update RTOS shared data
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                systemData.channel[0].enabled = true;
                systemData.channel[1].enabled = true;
                xSemaphoreGive(dataMutex);
            }
            Serial.println("Continuous mode STARTED");
        }
        else if (input == "STOP") {
            pcnt_pause();
            continuous_mode = false;
            counter_enabled = false;
            
            // Update RTOS shared data
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                systemData.channel[0].enabled = false;
                systemData.channel[1].enabled = false;
                xSemaphoreGive(dataMutex);
            }
            Serial.println("Continuous mode STOPPED");
        }
        else if (input == "PAUSE") {
            pcnt_pause();
            timer_pause(FREQ_TIMER_GROUP, FREQ_TIMER_IDX);
            Serial.println("PCNT & Timer Paused");
        }
        else if (input == "RESUME") {
            pcnt_resume();
            timer_start(FREQ_TIMER_GROUP, FREQ_TIMER_IDX);
            Serial.println("PCNT & Timer Resumed");
        }
        else if (input == "STATUS") {
            Serial.println("\n=== SYSTEM STATUS ===");
            Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
            Serial.println("--- Channel 1 ---");
            Serial.printf("  Count: %ld\n", (long)pcnt_ch1_get_count());
            Serial.printf("  Frequency: %ld Hz\n", (long)s_ch1_frequency_hz);
            Serial.printf("  Errors: %lu\n", (unsigned long)s_ch1_error_count);
            Serial.println("--- Channel 2 ---");
            Serial.printf("  Count: %ld\n", (long)pcnt_ch2_get_count());
            Serial.printf("  Frequency: %ld Hz\n", (long)s_ch2_frequency_hz);
            Serial.println("--- Timer ---");
            Serial.printf("  Enabled: %s\n", timer_enabled ? "YES" : "NO");
            Serial.printf("  Running: %s\n", stopwatch_running ? "YES" : "NO");
            Serial.printf("  Elapsed: %lu ms\n", (unsigned long)elapsed_ms);
            Serial.println("--- Tasks ---");
            Serial.printf("  Counter Task: %s\n", counterTaskHandle ? "Running" : "NULL");
            Serial.printf("  Timer Task: %s\n", timerTaskHandle ? "Running" : "NULL");
            Serial.printf("  Modbus Task: %s\n", modbusTaskHandle ? "Running" : "NULL");
            Serial.printf("  Web Task: %s\n", webTaskHandle ? "Running" : "NULL");
            Serial.println("=====================\n");
        }
        else if (input == "TASKS") {
            // Print task info (simplified - vTaskList requires special config)
            Serial.println("\n=== RTOS TASKS ===");
            Serial.printf("Counter Task: %s (Core %d)\n", 
                          counterTaskHandle ? "Running" : "NULL", CORE_REALTIME);
            Serial.printf("Timer Task: %s (Core %d)\n", 
                          timerTaskHandle ? "Running" : "NULL", CORE_REALTIME);
            Serial.printf("Input Task: %s (Core %d)\n", 
                          inputTaskHandle ? "Running" : "NULL", CORE_REALTIME);
            Serial.printf("Modbus Task: %s (Core %d)\n", 
                          modbusTaskHandle ? "Running" : "NULL", CORE_COMMS);
            Serial.printf("Web Task: %s (Core %d)\n", 
                          webTaskHandle ? "Running" : "NULL", CORE_COMMS);
            Serial.println("==================\n");
        }
        else if (input == "TIMER ON") {
            timer_enabled = true;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                systemData.timer.enabled = true;
                xSemaphoreGive(dataMutex);
            }
            Serial.println("Timer ENABLED");
        }
        else if (input == "TIMER OFF") {
            timer_enabled = false;
            stopwatch_pause();
            stopwatch_running = false;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                systemData.timer.enabled = false;
                systemData.timer.running = false;
                xSemaphoreGive(dataMutex);
            }
            Serial.println("Timer DISABLED");
        }
        // =================================================================
        // INPUT MODE COMMANDS
        // =================================================================
        else if (input.startsWith("MODE ")) {
            // Parse: MODE CH1 UP, MODE CH2 UDC, etc.
            String args = input.substring(5);
            args.trim();
            
            int ch = 0;
            InputMode mode = MODE_UP;
            bool valid = false;
            
            // Parse channel
            if (args.startsWith("CH1 ") || args.startsWith("1 ")) {
                ch = 1;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            } else if (args.startsWith("CH2 ") || args.startsWith("2 ")) {
                ch = 2;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            }
            
            // Parse mode
            if (ch > 0) {
                if (args == "UP") {
                    mode = MODE_UP;
                    valid = true;
                } else if (args == "DN" || args == "DOWN") {
                    mode = MODE_DN;
                    valid = true;
                } else if (args == "UDA") {
                    mode = MODE_UDA;
                    valid = true;
                } else if (args == "UDC" || args == "QUAD" || args == "ENCODER") {
                    mode = MODE_UDC;
                    valid = true;
                }
            }
            
            if (valid) {
                ChannelInputConfig_t cfg = input_config_get(ch - 1);
                cfg.input_mode = mode;
                esp_err_t err = input_config_set_mode(ch - 1, &cfg);
                if (err == ESP_OK) {
                    Serial.printf("CH%d mode set to %s\n", ch, input_mode_to_string(mode));
                    
                    // Update RTOS shared data
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        systemData.channel[ch - 1].input_mode = mode;
                        xSemaphoreGive(dataMutex);
                    }
                    
                    // Auto-save to NVS
                    nvs_save_input_mode(ch - 1, mode);
                } else {
                    Serial.printf("ERROR: Failed to set mode (err=%d)\n", err);
                }
            } else {
                Serial.println("Usage: MODE CH1|CH2 UP|DN|UDA|UDC");
            }
        }
        else if (input.startsWith("EDGE ")) {
            // Parse: EDGE CH1 RISING, EDGE CH2 FALLING, EDGE CH1 BOTH
            String args = input.substring(5);
            args.trim();
            
            int ch = 0;
            EdgeMode edge = EDGE_RISING;
            bool valid = false;
            
            if (args.startsWith("CH1 ") || args.startsWith("1 ")) {
                ch = 1;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            } else if (args.startsWith("CH2 ") || args.startsWith("2 ")) {
                ch = 2;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            }
            
            if (ch > 0) {
                if (args == "RISING" || args == "RISE") {
                    edge = EDGE_RISING;
                    valid = true;
                } else if (args == "FALLING" || args == "FALL") {
                    edge = EDGE_FALLING;
                    valid = true;
                } else if (args == "BOTH") {
                    edge = EDGE_BOTH;
                    valid = true;
                }
            }
            
            if (valid) {
                ChannelInputConfig_t cfg = input_config_get(ch - 1);
                cfg.edge_mode = edge;
                esp_err_t err = input_config_set_mode(ch - 1, &cfg);
                if (err == ESP_OK) {
                    Serial.printf("CH%d edge set to %s\n", ch, 
                        edge == EDGE_RISING ? "RISING" : edge == EDGE_FALLING ? "FALLING" : "BOTH");
                    
                    // Auto-save to NVS
                    nvs_save_edge_mode(ch - 1, edge);
                } else {
                    Serial.printf("ERROR: Failed to set edge (err=%d)\n", err);
                }
            } else {
                Serial.println("Usage: EDGE CH1|CH2 RISING|FALLING|BOTH");
            }
        }
        else if (input == "MODES") {
            Serial.println("\n=== INPUT MODE STATUS ===");
            for (int ch = 0; ch < 2; ch++) {
                ChannelInputConfig_t cfg = input_config_get(ch);
                Serial.printf("CH%d: Mode=%s, Edge=%s\n", 
                    ch + 1,
                    input_mode_to_string(cfg.input_mode),
                    cfg.edge_mode == EDGE_RISING ? "RISING" : cfg.edge_mode == EDGE_FALLING ? "FALLING" : "BOTH");
            }
            Serial.println("=========================\n");
        }
        // =================================================================
        // NVS CONFIGURATION COMMANDS
        // =================================================================
        else if (input == "SAVE") {
            StoredConfig_t cfg;
            // Get current config from input_config
            ChannelInputConfig_t ch1 = input_config_get(0);
            ChannelInputConfig_t ch2 = input_config_get(1);
            
            cfg.ch1_input_mode = ch1.input_mode;
            cfg.ch1_edge_mode = ch1.edge_mode;
            cfg.ch1_filter = ch1.filter_value;
            cfg.ch1_preset_value = 10000; // TODO: Get from CT_counter
            
            cfg.ch2_input_mode = ch2.input_mode;
            cfg.ch2_edge_mode = ch2.edge_mode;
            cfg.ch2_filter = ch2.filter_value;
            cfg.ch2_preset_value = 10000; // TODO: Get from CT_counter
            
            cfg.modbus_address = MODBUS_SLAVE_ID;
            cfg.modbus_baud = MODBUS_BAUD_RATE;
            cfg.config_version = CONFIG_VERSION;
            
            if (nvs_config_save(&cfg)) {
                Serial.println("Configuration saved to NVS");
            } else {
                Serial.println("ERROR: Failed to save configuration");
            }
        }
        else if (input == "LOAD") {
            StoredConfig_t cfg;
            if (nvs_config_load(&cfg)) {
                nvs_config_apply(&cfg);
                Serial.println("Configuration loaded and applied");
            } else {
                Serial.println("Using default configuration");
            }
        }
        else if (input == "FACTORY") {
            if (nvs_config_reset()) {
                StoredConfig_t cfg;
                nvs_config_get_defaults(&cfg);
                nvs_config_apply(&cfg);
                Serial.println("Factory reset complete - defaults restored");
            } else {
                Serial.println("ERROR: Factory reset failed");
            }
        }
        // =================================================================
        // PRESET COMMANDS
        // =================================================================
        else if (input.startsWith("PRESET ")) {
            // Parse: PRESET CH1 10000, PRESET CH2 5000
            String args = input.substring(7);
            args.trim();
            
            int ch = 0;
            int32_t preset_val = 0;
            bool valid = false;
            
            if (args.startsWith("CH1 ") || args.startsWith("1 ")) {
                ch = 1;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            } else if (args.startsWith("CH2 ") || args.startsWith("2 ")) {
                ch = 2;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            }
            
            if (ch > 0 && args.length() > 0) {
                preset_val = args.toInt();
                if (preset_val > 0 || args == "0") {
                    valid = true;
                }
            }
            
            if (valid) {
                CT_counter* ctr = getCounterInstance(ch);
                if (ctr) {
                    ctr->setPresetValue(preset_val);
                    Serial.printf("CH%d preset set to %ld\n", ch, (long)preset_val);
                } else {
                    Serial.printf("ERROR: Counter CH%d not initialized\n", ch);
                }
            } else {
                Serial.println("Usage: PRESET CH1|CH2 <value>");
            }
        }
        else if (input.startsWith("OUTPUT ")) {
            // Parse: OUTPUT CH1 N, OUTPUT CH2 F, OUTPUT CH1 C
            String args = input.substring(7);
            args.trim();
            
            int ch = 0;
            OutputMode out_mode = OUTPUT_N;
            bool valid = false;
            
            if (args.startsWith("CH1 ") || args.startsWith("1 ")) {
                ch = 1;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            } else if (args.startsWith("CH2 ") || args.startsWith("2 ")) {
                ch = 2;
                args = args.substring(args.indexOf(' ') + 1);
                args.trim();
            }
            
            if (ch > 0) {
                if (args == "N" || args == "NORMAL") {
                    out_mode = OUTPUT_N;
                    valid = true;
                } else if (args == "F" || args == "ONESHOT") {
                    out_mode = OUTPUT_F;
                    valid = true;
                } else if (args == "C" || args == "CARRY") {
                    out_mode = OUTPUT_C;
                    valid = true;
                } else if (args == "NONE" || args == "OFF") {
                    out_mode = OUTPUT_NONE;
                    valid = true;
                }
            }
            
            if (valid) {
                CT_counter* ctr = getCounterInstance(ch);
                if (ctr) {
                    ctr->setOutputMode(out_mode);
                    Serial.printf("CH%d output mode set\n", ch);
                } else {
                    Serial.printf("ERROR: Counter CH%d not initialized\n", ch);
                }
            } else {
                Serial.println("Usage: OUTPUT CH1|CH2 N|F|C|NONE");
            }
        }
        else if (input == "COUNTERS") {
            Serial.println("\n=== COUNTER STATUS ===");
            for (int ch = 1; ch <= 2; ch++) {
                CT_counter* ctr = getCounterInstance(ch);
                if (ctr) {
                    Serial.printf("CH%d: Count=%ld, Preset=%ld, Enabled=%s, Output=%s\n",
                        ch,
                        (long)ctr->getCurrentValue(),
                        (long)ctr->getPresetValue(),
                        ctr->isEnabled() ? "YES" : "NO",
                        ctr->getOutputState() ? "ON" : "OFF");
                } else {
                    Serial.printf("CH%d: Not initialized\n", ch);
                }
            }
            Serial.println("======================\n");
        }
        else if (input == "HELP") {
            Serial.println("\n=== COMMANDS ===");
            Serial.println("RESET    - Reset all counters");
            Serial.println("START    - Start continuous counting");
            Serial.println("STOP     - Stop continuous counting");
            Serial.println("PAUSE    - Pause counters and timer");
            Serial.println("RESUME   - Resume counters and timer");
            Serial.println("STATUS   - Show system status");
            Serial.println("TASKS    - Show RTOS task list");
            Serial.println("TIMER ON - Enable timer mode");
            Serial.println("TIMER OFF- Disable timer mode");
            Serial.println("--- Input Mode ---");
            Serial.println("MODE CH1|CH2 UP|DN|UDA|UDC");
            Serial.println("  UP  - Count up only (INA)");
            Serial.println("  DN  - Count down only (INA)");
            Serial.println("  UDA - Up/Down separate (INA=up, INB=down)");
            Serial.println("  UDC - Quadrature encoder (INA+INB)");
            Serial.println("EDGE CH1|CH2 RISING|FALLING|BOTH");
            Serial.println("MODES    - Show input mode status");
            Serial.println("--- Counter Preset ---");
            Serial.println("PRESET CH1|CH2 <value>");
            Serial.println("OUTPUT CH1|CH2 N|F|C|NONE");
            Serial.println("COUNTERS - Show counter status");
            Serial.println("--- Storage ---");
            Serial.println("SAVE     - Save config to NVS");
            Serial.println("LOAD     - Load config from NVS");
            Serial.println("FACTORY  - Reset to factory defaults");
            Serial.println("HELP     - Show this help");
            Serial.println("================\n");
        }
        else if (input.length() > 0) {
            Serial.println("Unknown command. Type HELP for list.");
        }
    }
    
    // Continuous mode display (rate-limited)
    static unsigned long lastPrint = 0;
    if (continuous_mode && (millis() - lastPrint > 500)) {
        Serial.printf("[CH1] Count: %ld | Freq: %ld Hz  [CH2] Count: %ld | Freq: %ld Hz\n", 
                      (long)pcnt_ch1_get_count(), (long)s_ch1_frequency_hz,
                      (long)pcnt_ch2_get_count(), (long)s_ch2_frequency_hz);
        lastPrint = millis();
    }
}
