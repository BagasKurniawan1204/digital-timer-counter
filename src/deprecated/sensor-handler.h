#ifndef SENSOR_HANDLER_H
#define SENSOR_HANDLER_H

#include <Arduino.h>
#include "timer-operation.h"

#define SENSOR_PIN 14
#define DEBOUNCE_MS 20

bool lastState = LOW;
unsigned long lastChangeMillis = 0;
bool handled_current_high = false;

void sensor_init() {
    pinMode(SENSOR_PIN, INPUT_PULLUP);
}

void sensor_loop() {
    if (!timer_enabled) return;

    bool raw = digitalRead(SENSOR_PIN);
    unsigned long now = millis();

    if (raw != lastState) {
        lastChangeMillis = now;
        lastState = raw;
    } else {
        if ((now - lastChangeMillis) >= DEBOUNCE_MS) {
            if (raw == HIGH) {
                if (!handled_current_high) {
                    handled_current_high = true;
                    if (!stopwatch_running) {
                        Serial.println("RISING EDGE -> START stopwatch");
                        stopwatch_start();
                        stopwatch_running = true;
                    } else {
                        Serial.println("RISING EDGE -> PAUSE stopwatch");
                        stopwatch_pause();
                        stopwatch_running = false;
                    }
                }
            } else {
                if (handled_current_high) {
                    Serial.println("SENSOR RELEASED -> clear latch");
                }
                handled_current_high = false;
            }
        }
    }

    if (stopwatch_running) {
        uint64_t elapsed_us = 0;
        stopwatch_get_elapsed(&elapsed_us);
        elapsed_ms = elapsed_us / 1000ULL;
        Serial.print("Stopwatch running... Elapsed time (ms): ");
        Serial.println(elapsed_ms);
    }
}

#endif
