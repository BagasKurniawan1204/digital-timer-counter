#include <Arduino.h>

void setup(){
  Serial.begin(115200);
  pinMode(14,INPUT_PULLUP); //Pin 14 as signal input
}

 
void loop() {
  if(digitalRead(14) == LOW){
    Serial.println("Collision Detected");
  } else {
    Serial.println("Collision Not Detected");
  }
  delay(500);
}