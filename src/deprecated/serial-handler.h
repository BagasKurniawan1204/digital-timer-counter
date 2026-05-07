#ifndef SERIAL_HANDLER_H
#define SERIAL_HANDLER_H

#include <Arduino.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"
#include "web-server.h"

void serial_handler_loop() {
  if(Serial.available()){
    String input = Serial.readString();
    input.trim();
    input.toUpperCase();
    
    if(input == "RESET"){
      pcnt_reset();
      s_last_count_ch1 = 0;
      s_frequency_hz_ch1 = 0;
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
      Serial.print(s_frequency_hz_ch1);
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
    s_current_count_ch1 = pcnt_get_total_value();
    
    // Display and push when new frequency is available
    if(s_new_frequency_available) {
      Serial.print("Count: ");
      Serial.print(s_current_count_ch1);
      Serial.print(" | Frequency: ");
      Serial.print(s_frequency_hz_ch1);
      Serial.println(" Hz");

      s_new_frequency_available = false;
    }
  }

  // WebSocket push logic (Unified for both Counter and Timer)
  static unsigned long last_ws_send = 0;
  if (millis() - last_ws_send > 100) { // 10Hz update rate
      if (continuous_mode || stopwatch_running) {
          sendRealtimeData();
      }
      last_ws_send = millis();
  }
}

#endif
