#include <Arduino.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"
#include "web-server.h"

#define SENSOR_PIN 14
#define DEBOUNCE_MS 20 // 20 ms in milliseconds

bool lastState = LOW;
unsigned long lastChangeMillis = 0;
bool stopwatch_running = false;
bool handled_current_high = false;

portMUX_TYPE pcnt_spinlock = portMUX_INITIALIZER_UNLOCKED;

void setup(){
  Serial.begin(115200);
  pinMode(SENSOR_PIN, INPUT_PULLUP);

  // Start hw modules and server
  pcnt_init();
  freq_timer_init();
  stopwatch_timer_init();
  initWebServer();
}

void loop() {
  // cleanup websocket clients to avoid zombies
  ws.cleanupClients();
  // Start of command processing
  if(Serial.available()){
    String input = Serial.readString();
    input.trim();
    input.toUpperCase();
    
    if(input == "RESET"){
      pcnt_reset();
      s_last_count = 0;
      s_frequency_hz = 0;
      Serial.println("Encoder count and frequency reset.");
    }
    else if(input == "START" || input == "CONTINUOUS"){
      continuous_mode = true;
      pcnt_resume();
      Serial.println("Continuous mode STARTED");
    }
    else if(input == "STOP"){
      pcnt_pause();
      continuous_mode = false;
      Serial.println("Continuous mode STOPPED");
    }
    else if(input == "PAUSE"){
      pcnt_pause();
      timer_pause(TIMER_GROUP_0, TIMER_0);
      Serial.println("PCNT & Timer Paused.");
    }
    else if(input == "RESUME"){
      pcnt_resume();
      timer_start(TIMER_GROUP_0, TIMER_0);
      Serial.println("PCNT & Timer Resumed.");
    }
    else if(input == "STATUS"){
      Serial.print("Total Count: ");
      Serial.println(pcnt_get_total_value());
      Serial.print("Frequency: ");
      Serial.print(s_frequency_hz);
      Serial.println(" Hz");
      Serial.print("Errors: ");
      Serial.println(pcnt_get_error_count());
    }
    else{
      Serial.println("Unknown command. Use: RESET, START, STOP, PAUSE, RESUME, STATUS");
    }
  }

  // Improved continuous mode with frequency display and websocket push
  if(continuous_mode) {
    s_current_count = pcnt_get_total_value();
    
    // Display and push when new frequency is available
    if(s_new_frequency_available) {
      Serial.print("Count: ");
      Serial.print(s_current_count);
      Serial.print(" | Frequency: ");
      Serial.print(s_frequency_hz);
      Serial.println(" Hz");

      // push to websocket clients
      sendRealtimeData();

      s_new_frequency_available = false;
    }
    delay(5);
  }
  // Sensor reading with debounce and stopwatch control
  bool raw = digitalRead(SENSOR_PIN); // HIGH = detected
  unsigned long now = millis();

  if (raw != lastState) {
    // state changed — start debounce timer and update lastState
    lastChangeMillis = now;
    lastState = raw;
    // debug
    // Serial.printf("State changed to %d, start debounce\n", raw);
  } else {
    // state stable long enough?
    if ((now - lastChangeMillis) >= DEBOUNCE_MS) {
      if (raw == HIGH) {
        // confirmed stable HIGH
        if (!handled_current_high) {
          // first time seeing this HIGH -> handle rising action
          handled_current_high = true; // latch until a LOW is seen (and stable)
          if (!stopwatch_running) {
            Serial.println("RISING EDGE -> START stopwatch");
            stopwatch_start(); // make sure this function exists in timer-operation.h
            stopwatch_running = true;
          } else {
            Serial.println("RISING EDGE -> PAUSE stopwatch");
            stopwatch_pause(); // or stopwatch_stop() if you want reset
            stopwatch_running = false;
          }
        }
      } else { // raw == LOW
        // confirmed stable LOW -> clear latch so next rising will be handled
        if (handled_current_high) {
          Serial.println("SENSOR RELEASED -> clear latch, ready for next rising");
        }
        handled_current_high = false;
      }
    }
  }

  // Optional: display elapsed while running
  if (stopwatch_running) {
    uint64_t elapsed_us = 0;
    stopwatch_get_elapsed(&elapsed_us); // helper in your header. :contentReference[oaicite:3]{index=3}
    Serial.print("Stopwatch running... Elapsed time (ms): ");
    Serial.println(elapsed_us / 1000ULL);
  }
  delay(5);
}
