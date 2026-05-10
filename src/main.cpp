/**
 * @file main.cpp
 * @brief Main entry point for ESP32 Timer & Counter device
 * 
 * CT4-style High Speed Counter and Timer with:
 * - Dual-core FreeRTOS task architecture
 * - Modbus RTU communication
 * - Web configurator interface
 * - Flexible input modes (UP, DN, UDA, UDC)
 * 
 * Core 0: Communication (WiFi, WebServer, Modbus)
 * Core 1: Real-time processing (Counter, Timer, Input)
 */

#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "timer_operation.h"
#include "web_server.h"
#include "modbus_rtu.h"
#include "sensor_handler.h"
#include "serial_handler.h"
#include "rtos_tasks.h"
#include "nvs_config.h"
#include "CT_counter.h"
#include "ui.h"

// Global counter instances
CT_counter* counter1 = nullptr;
CT_counter* counter2 = nullptr;

void setup() {
    Serial.begin(115200);
    delay(100);  // Allow serial to initialize
    
    Serial.println("\n========================================");
    Serial.println("ESP32 HSC Timer");
    Serial.println("========================================");
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion());
    Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("----------------------------------------");
    
    // =========================================================================
    // PHASE 1: Initialize RTOS resources (before hardware)
    // =========================================================================
    Serial.println("[1/5] Initializing RTOS resources...");
    rtos_init();
    
    // =========================================================================
    // PHASE 2: Load configuration from NVS
    // =========================================================================
    Serial.println("[2/6] Loading configuration from NVS...");
    StoredConfig_t storedConfig;
    if (nvs_config_init()) {
        nvs_config_load(&storedConfig);
    } else {
        Serial.println("  WARNING: NVS init failed, using defaults");
        nvs_config_get_defaults(&storedConfig);
    }
    
    // =========================================================================
    // PHASE 3: Initialize hardware
    // =========================================================================
    Serial.println("[3/6] Initializing hardware...");
    
    // Output pins for preset comparison
    pinMode(OUTPUT_CH1_PIN, OUTPUT);
    pinMode(OUTPUT_CH2_PIN, OUTPUT);
    digitalWrite(OUTPUT_CH1_PIN, LOW);
    digitalWrite(OUTPUT_CH2_PIN, LOW);
    Serial.printf("  Output pins: CH1=GPIO%d, CH2=GPIO%d\n", OUTPUT_CH1_PIN, OUTPUT_CH2_PIN);
    
    // Sensor input pin
    sensor_init();
    
    // PCNT counter initialization
    esp_err_t ret = pcnt_init();
    if (ret != ESP_OK) {
        Serial.println("  ERROR: PCNT init failed!");
    } else {
        Serial.println("  PCNT initialized OK");
    }
    
    // =========================================================================
    // PHASE 4: Initialize timers
    // =========================================================================
    Serial.println("[4/6] Initializing timers...");
    freq_timer_init();
    stopwatch_timer_init();
    
    // =========================================================================
    // PHASE 5: Initialize communication interfaces
    // =========================================================================
    Serial.println("[5/6] Initializing communication...");
    web_server_init();
    modbus_init();
    
    // =========================================================================
    // PHASE 6: Apply stored configuration
    // =========================================================================
    Serial.println("[6/6] Applying stored configuration...");
    nvs_config_apply(&storedConfig);
    
    // =========================================================================
    // Create CT_counter instances
    // =========================================================================
    Serial.println("Creating CT_counter instances...");
    counter1 = new CT_counter(1);
    counter2 = new CT_counter(2);
    
    // Apply stored preset values
    counter1->setPresetValue(storedConfig.ch1_preset_value);
    counter2->setPresetValue(storedConfig.ch2_preset_value);
    
    // =========================================================================
    // Start RTOS tasks
    // =========================================================================
    Serial.println("Starting RTOS tasks...");
    rtos_start_tasks();
    
    // =========================================================================
    // Initialize TFT UI
    // =========================================================================
    Serial.println("Initializing TFT UI...");
    
    // Explicitly set CS pins high before initializing the bus
    pinMode(4, OUTPUT);       // TOUCH_CS
    digitalWrite(4, HIGH);
    pinMode(22, OUTPUT);      // TFT_CS
    digitalWrite(22, HIGH);
    
    pinMode(13, OUTPUT);      // TFT_BL
    digitalWrite(13, HIGH);   // Backlight ON
    
    ui_init();                // Setup display & draw layout
    pcnt_resume();
    
    Serial.println("----------------------------------------");
    Serial.println("System ready! Commands: HELP, STATUS, RESET");
    Serial.println("========================================\n");
}

void loop() {
    // Main loop runs on Core 1 with lowest priority
    // Most work is done by RTOS tasks, but we keep serial handler here
    // for debug convenience
    
    // Process serial commands (debug interface)
    serial_handler_loop();
    
    // Update the TFT display every 200ms
    static unsigned long last_ui_update = 0;
    if (millis() - last_ui_update > 200) {
        last_ui_update = millis();
        // Ensure counters are instantiated before querying
        if (counter1 != nullptr && counter2 != nullptr) {
            ui_update_counter(
                counter1->getCurrentValue(), counter1->getPresetValue(),
                counter2->getCurrentValue(), counter2->getPresetValue()
            );
        }
    }
    
    // Small delay to prevent watchdog issues
    vTaskDelay(pdMS_TO_TICKS(10));
}
