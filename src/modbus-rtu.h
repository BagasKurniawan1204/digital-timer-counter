#include <Arduino.h>
#include <ModbusRTU.h>
#include "pcnt-configuration.h"
#include "timer-operation.h"
#define RX2_PIN 16
#define TX2_PIN 17

#define SLAVE_ID 1

ModbusRTU mb;
extern volatile uint32_t elapsed_ms;
// Callback function for Read-Only registers
uint16_t cb_RO(TRegister* reg, uint16_t val) {
    // ON_SET dipanggil saat master mau menulis
    // Kita TOLAK write: return EX_ILLEGAL_FUNCTION
    return Modbus::EX_ILLEGAL_FUNCTION;
}

// Callback function for Write-Only registers
uint16_t cb_WO(TRegister* reg, uint16_t val) {
    // ON_GET dipanggil saat master mau READ
    // Return 0x0000 atau error
    return Modbus::EX_ILLEGAL_FUNCTION;
}

// Callback function for Validated registers (Enable Timer - 104)
uint16_t cb_EnableTimer(TRegister* reg, uint16_t val) {
    if (val > 1) {
        return Modbus::EX_ILLEGAL_VALUE; // tolak
    }
    reg->value = val; // simpan
    timer_enabled = (val == 1);
    return Modbus::EX_SUCCESS;
}

// Callback function for Reset Timer (105)
uint16_t cb_ResetTimer(TRegister* reg, uint16_t val) {
    if (val == 1) {
        stopwatch_reset();
        elapsed_ms = 0;
        reg->value = 0; // Auto reset register to 0
        return Modbus::EX_SUCCESS;
    }
    return Modbus::EX_SUCCESS;
}

// Callback function to get PCNT Channel 1 Count
// Returns 32-bit count split into two 16-bit registers (Little Endian Word Order)
// Reg 100: Low Word. If viewed as signed 16-bit, values 32768-65535 appear negative (-32768 to -1).
// Reg 101: High Word. Only increments when count exceeds 65535.
// To read correctly: Configure Modbus Master to read "32-bit Signed Int" (Little Endian / LSW First) at address 100.
uint16_t cb_GetPCNTch1(TRegister* reg, uint16_t val){
    int32_t count = pcnt_ch1_get_count();
    uint16_t addr = reg->address.address;

    switch(addr) {
        case 100:
            return (uint16_t)(count & 0xFFFF);            // lower word
        case 101:
            return (uint16_t)((count >> 16) & 0xFFFF);    // upper word
    }
    return 0;
}

// Callback function to get Frequency Channel 1 HSC
uint16_t cb_GetFrequencyCh1(TRegister* reg, uint16_t val){
    return (uint16_t)s_frequency_hz_ch1;
}
// Callback function to get PCNT Channel 2 Count
// uint16_t cb_GetPCNTch2(TRegister* reg, uint16_t val){
//     int32_t count = pcnt_ch2_get_count();
//     uint16_t addr = reg->address.address;

//     switch(addr) {
//         case 103:
//             return (uint16_t)(count & 0xFFFF);            // lower word
//         case 104:
//             return (uint16_t)((count >> 16) & 0xFFFF);    // upper word
//     }
//     return 0;
// }

//Callback function to get Elapsed timer value
uint16_t cb_GetElapsedTimer(TRegister* reg, uint16_t val){
    return (uint16_t)(elapsed_ms & 0xFFFF);
}
void modbus_init() {
    // 9600 baud, 8 data bits, No parity, 1 stop bit
    Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);
    Serial.println("Modbus Slave Starting...");
    mb.begin(&Serial2); 
    mb.slave(SLAVE_ID);

    //Add Registers; Argument: address, initial value, number of registers
    //100 Channel 1 HSC Count Upper WORD; Read Only
    //101 Channel 1 HSC Count Lower WORD; Read Only
    //102 Frequency Channel 1 HSC; Read Only
    //103 Elapsed Timer in ms; Read Only
    //104 Enable HSC; Read/Write
    //105 Reset Timer; Write Only
    mb.addHreg(100, 0, 6); //100-105
    mb.onGet(HREG(100), cb_GetPCNTch1, 2);
    mb.onSet(HREG(100), cb_RO);
    mb.onSet(HREG(101), cb_RO);
    mb.onGet(HREG(102), cb_GetFrequencyCh1);
    mb.onSet(HREG(102), cb_RO);
    mb.onGet(HREG(103), cb_GetElapsedTimer);
    mb.onSet(HREG(103), cb_RO);
    mb.onSet(HREG(104), cb_EnableTimer);
    mb.onGet(HREG(104), [](TRegister* r, uint16_t v)->uint16_t { return timer_enabled ? 1 : 0; }); // allow read of enable
    mb.onSet(HREG(105), cb_ResetTimer);
    mb.onGet(HREG(105), [](TRegister* r, uint16_t v)->uint16_t { return 0; });
}