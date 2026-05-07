/**
 * @file modbus_rtu.cpp
 * @brief Modbus RTU slave implementation
 */

#include "modbus_rtu.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "timer_operation.h"

// Modbus object definition
ModbusRTU mb;

// =============================================================================
// CALLBACK FUNCTIONS
// =============================================================================

/**
 * @brief Callback for read-only registers (reject writes)
 */
static uint16_t cb_ReadOnly(TRegister* reg, uint16_t val) {
    return Modbus::EX_ILLEGAL_FUNCTION;
}

/**
 * @brief Callback to get PCNT Channel 1 Count (Low Word)
 */
static uint16_t cb_GetCountLo(TRegister* reg, uint16_t val) {
    int32_t count = pcnt_ch1_get_count();
    return (uint16_t)(count & 0xFFFF);
}

/**
 * @brief Callback to get PCNT Channel 1 Count (High Word)
 */
static uint16_t cb_GetCountHi(TRegister* reg, uint16_t val) {
    int32_t count = pcnt_ch1_get_count();
    return (uint16_t)((count >> 16) & 0xFFFF);
}

/**
 * @brief Callback to get Frequency Channel 1
 */
static uint16_t cb_GetFrequency(TRegister* reg, uint16_t val) {
    return (uint16_t)s_ch1_frequency_hz;
}

/**
 * @brief Callback to get Elapsed Timer value (ms)
 */
static uint16_t cb_GetElapsedTimer(TRegister* reg, uint16_t val) {
    return (uint16_t)(elapsed_ms & 0xFFFF);
}

/**
 * @brief Callback for Timer Enable register (validated write)
 */
static uint16_t cb_SetTimerEnable(TRegister* reg, uint16_t val) {
    if (val > 1) {
        return Modbus::EX_ILLEGAL_VALUE;
    }
    reg->value = val;
    timer_enabled = (val == 1);
    return Modbus::EX_SUCCESS;
}

/**
 * @brief Callback for Timer Enable register (read)
 */
static uint16_t cb_GetTimerEnable(TRegister* reg, uint16_t val) {
    return timer_enabled ? 1 : 0;
}

/**
 * @brief Callback for Timer Reset register (write-only)
 */
static uint16_t cb_SetTimerReset(TRegister* reg, uint16_t val) {
    if (val == 1) {
        stopwatch_reset();
        elapsed_ms = 0;
        reg->value = 0; // Auto-reset register
    }
    return Modbus::EX_SUCCESS;
}

/**
 * @brief Callback for Timer Reset register (read returns 0)
 */
static uint16_t cb_GetTimerReset(TRegister* reg, uint16_t val) {
    return 0;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void modbus_init() {
    // Configure Serial2 for RS485
    Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    
    Serial.println("Modbus RTU Slave initializing...");
    
    // Initialize Modbus slave
    mb.begin(&Serial2);
    mb.slave(MODBUS_SLAVE_ID);
    
    // Add holding registers (100-105)
    mb.addHreg(MB_REG_CH1_COUNT_LO, 0, MB_REG_COUNT);
    
    // Register 100: Channel 1 Count Low Word (RO)
    mb.onGet(HREG(MB_REG_CH1_COUNT_LO), cb_GetCountLo);
    mb.onSet(HREG(MB_REG_CH1_COUNT_LO), cb_ReadOnly);
    
    // Register 101: Channel 1 Count High Word (RO)
    mb.onGet(HREG(MB_REG_CH1_COUNT_HI), cb_GetCountHi);
    mb.onSet(HREG(MB_REG_CH1_COUNT_HI), cb_ReadOnly);
    
    // Register 102: Channel 1 Frequency (RO)
    mb.onGet(HREG(MB_REG_CH1_FREQUENCY), cb_GetFrequency);
    mb.onSet(HREG(MB_REG_CH1_FREQUENCY), cb_ReadOnly);
    
    // Register 103: Elapsed Timer ms (RO)
    mb.onGet(HREG(MB_REG_ELAPSED_TIMER), cb_GetElapsedTimer);
    mb.onSet(HREG(MB_REG_ELAPSED_TIMER), cb_ReadOnly);
    
    // Register 104: Timer Enable (RW)
    mb.onGet(HREG(MB_REG_TIMER_ENABLE), cb_GetTimerEnable);
    mb.onSet(HREG(MB_REG_TIMER_ENABLE), cb_SetTimerEnable);
    
    // Register 105: Timer Reset (WO)
    mb.onGet(HREG(MB_REG_TIMER_RESET), cb_GetTimerReset);
    mb.onSet(HREG(MB_REG_TIMER_RESET), cb_SetTimerReset);
    
    Serial.printf("Modbus RTU Slave started: ID=%d, Baud=%d\n", 
                  MODBUS_SLAVE_ID, MODBUS_BAUD_RATE);
}
