#include <Arduino.h>
#include <ModbusRTU.h>

// Define the serial pins for ESP32 (using Serial2)
#define RX2_PIN 16
#define TX2_PIN 17

// Define the Modbus Slave ID
#define SLAVE_ID 1

// Create the ModbusRTU object
ModbusRTU mb;

void setup() {
    // 1. Initialize Serial2 for RS485 communication
    // 9600 baud, 8 data bits, No parity, 1 stop bit (8N1 is standard)
    Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

    // 2. Initialize Serial0 for debugging (USB to PC)
    Serial.begin(115200);
    Serial.println("Modbus Slave Starting...");

    // 3. Configure the Modbus object to use Serial2
    // No DE/RE pin needed because you have an Auto-flow module
    mb.begin(&Serial2); 

    // 4. Set the Slave ID
    mb.slave(SLAVE_ID);

    // 5. Add a Holding Register (Hreg)
    // Arguments: (Address, Initial Value)
    // We are adding a register at address 100
    mb.addHreg(100, 0);
}

void loop() {
    // Essential: This processes the Modbus logic
    mb.task();

    // Simulating a changing sensor value
    // Let's increment the value inside register 100 every second
    static unsigned long lastMillis = 0;
    if (millis() - lastMillis > 1000) {
        lastMillis = millis();
        
        // Read the current value
        uint16_t currentVal = mb.Hreg(100);
        
        // Update the value (0 to 1000 loop)
        uint16_t newVal = (currentVal + 1) % 1000;
        mb.Hreg(100, newVal);
        
        // Print to debug console so you know it's working internally
        Serial.printf("Register 100 updated to: %d\n", newVal);
    }
    
    yield();
}