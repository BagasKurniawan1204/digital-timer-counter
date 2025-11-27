#include <Arduino.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"
#include "web-server.h"

#define SENSOR_PIN 14
#define DEBOUNCE_MS 20 // 20 ms in milliseconds
bool lastState = LOW;
unsigned long lastChangeMillis = 0;
bool triggered = false; // one-shot latch

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
  bool raw = digitalRead(SENSOR_PIN); // HIGH = detected in kode kamu
  unsigned long now = millis();

  if (raw != lastState) {
    // state changed — start debounce timer
    lastChangeMillis = now;
    lastState = raw;
  } else {
    if (!triggered && raw == HIGH && (now - lastChangeMillis) >= DEBOUNCE_MS) {
      // rising edge confirmed
      Serial.println("RISING EDGE -> START stopwatch");
      stopwatch_start(); // from your timer header. :contentReference[oaicite:3]{index=3}
      triggered = true; // prevent retrigger until reset condition
    }

    // example: reset the latch when sensor goes LOW again (so next rising can trigger)
    if (triggered && raw == LOW && (now - lastChangeMillis) >= DEBOUNCE_MS) {
      Serial.println("SENSOR RELEASED -> allow next trigger");
      triggered = false;
      stopwatch_stop(); // or pause/reset as your logic needs. :contentReference[oaicite:4]{index=4}
    }
  }
  
  if(triggered){
    Serial.print("Stopwatch running... Elapsed time (ms): ");
    uint64_t elapsed;
    timer_get_counter_value(TIMER_GROUP_1, TIMER_0, &elapsed);
    Serial.println(elapsed / 1000); // convert microseconds to milliseconds
  }
  delay(5);
}
