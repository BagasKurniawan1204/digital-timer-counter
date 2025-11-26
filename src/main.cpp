#include <Arduino.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"
#include "web-server.h"

portMUX_TYPE pcnt_spinlock = portMUX_INITIALIZER_UNLOCKED;

void setup(){
  Serial.begin(115200);
  
  // Start your hw modules and server
  pcnt_init();
  timer_init();
  initWebServer();
}

void loop() {
  // cleanup websocket clients to avoid zombies
  ws.cleanupClients();

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
    delay(100);
  }
  
  delay(5);
}
