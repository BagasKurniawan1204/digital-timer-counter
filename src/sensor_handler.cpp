/**
 * @file sensor_handler.cpp
 * @brief External sensor input handling implementation
 */

#include "sensor_handler.h"
#include "config.h"
#include "globals.h"
#include "timer_operation.h"

// Internal state variables
static bool s_last_state = LOW;
static unsigned long s_last_change_millis = 0;
static bool s_handled_current_high = false;

// =============================================================================
// INITIALIZATION
// =============================================================================

void sensor_init() {
    pinMode(SENSOR_INPUT_PIN, INPUT_PULLUP);
    Serial.printf("Sensor input initialized on GPIO%d\n", SENSOR_INPUT_PIN);
}

// =============================================================================
// SENSOR PROCESSING
// =============================================================================

void sensor_loop() {
    // Only process when timer is enabled
    if (!timer_enabled) {
        return;
    }
    
    bool raw = digitalRead(SENSOR_INPUT_PIN);
    unsigned long now = millis();
    
    // Detect state change - reset debounce timer
    if (raw != s_last_state) {
        s_last_change_millis = now;
        s_last_state = raw;
        return;
    }
    
    // Check if debounce period has passed
    if ((now - s_last_change_millis) < SENSOR_DEBOUNCE_MS) {
        return;
    }
    
    // Stable HIGH detected
    if (raw == HIGH) {
        if (!s_handled_current_high) {
            s_handled_current_high = true;
            
            if (!stopwatch_running) {
                Serial.println("SENSOR: Rising edge -> START stopwatch");
                stopwatch_start();
                stopwatch_running = true;
            } else {
                Serial.println("SENSOR: Rising edge -> PAUSE stopwatch");
                stopwatch_pause();
                stopwatch_running = false;
            }
        }
    } 
    // Stable LOW detected
    else {
        if (s_handled_current_high) {
            // Clear latch when sensor is released
            s_handled_current_high = false;
        }
    }
    
    // Update elapsed time when running
    if (stopwatch_running) {
        uint64_t elapsed_us = 0;
        stopwatch_get_elapsed(&elapsed_us);
        elapsed_ms = (uint32_t)(elapsed_us / 1000ULL);
    }
}
