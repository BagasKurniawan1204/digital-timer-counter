#include <Arduino.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"
#include "web-server.h"
#include "modbus-rtu.h"
#include "sensor-handler.h"
#include "serial-handler.h"

portMUX_TYPE pcnt_spinlock = portMUX_INITIALIZER_UNLOCKED;

void setup(){
  Serial.begin(115200);
  
  // Start hw modules and server
  pcnt_init();
  freq_timer_init();
  stopwatch_timer_init();
  sensor_init();
  web_server_init();
  modbus_init();
}

void loop() {
  ws.cleanupClients();
  mb.task();
  serial_handler_loop();
  sensor_loop();
  yield();
}
