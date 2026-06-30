/**
 * @file modbus_rtu.cpp
 * @brief Modbus RTU slave implementation
 */

#include "modbus_rtu.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "timer_operation.h"
#include "input_config.h"
#include "CT_counter.h"

// Modbus object definition
ModbusRTU mb;

// =============================================================================
// HELPER FOR INPUT MODE
// =============================================================================
static void change_input_mode(uint8_t channel, uint16_t val) {
    if (val > 3) return; // Safety check
    
    // Retrieve current full config so we don't overwrite edge mode/filter
    ChannelInputConfig_t new_config = input_config_get(channel);
    
    switch(val) {
        case 0: new_config.input_mode = MODE_UP; break;
        case 1: new_config.input_mode = MODE_DN; break;
        case 2: new_config.input_mode = MODE_UDA; break;
        case 3: new_config.input_mode = MODE_UDC; break;
    }
    
    input_config_set_mode(channel, &new_config);
}

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
 * @brief Always return 0 for WO (Write-Only) registers
 */
static uint16_t cb_ReturnZero(TRegister* reg, uint16_t val) {
    return 0;
}

// -----------------------------------------------------------------------------
// CHANNEL 1 CALLBACKS
// -----------------------------------------------------------------------------
static uint16_t cb_GetCh1CountLo(TRegister* reg, uint16_t val) {
    int32_t count = pcnt_ch1_get_count();
    return (uint16_t)(count & 0xFFFF);
}

static uint16_t cb_GetCh1CountHi(TRegister* reg, uint16_t val) {
    int32_t count = pcnt_ch1_get_count();
    return (uint16_t)((count >> 16) & 0xFFFF);
}

static uint16_t cb_GetCh1Frequency(TRegister* reg, uint16_t val) {
    return (uint16_t)s_ch1_frequency_hz;
}

static uint16_t cb_SetCh1Reset(TRegister* reg, uint16_t val) {
    if (val == 1) {
        pcnt_ch1_reset();
        reg->value = 0; // Auto-reset register back to 0
    }
    return Modbus::EX_SUCCESS;
}

static uint16_t cb_GetCh1Mode(TRegister* reg, uint16_t val) {
    return (uint16_t)input_config_get(0).input_mode;
}

static uint16_t cb_SetCh1Mode(TRegister* reg, uint16_t val) {
    if (val > 3) return Modbus::EX_ILLEGAL_VALUE;
    change_input_mode(0, val);
    reg->value = val;
    return Modbus::EX_SUCCESS;
}

static uint16_t cb_GetCh1TargetLo(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(1);
    if (!counter) return 0;
    return (uint16_t)(counter->getPresetValue() & 0xFFFF);
}

static uint16_t cb_GetCh1TargetHi(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(1);
    if (!counter) return 0;
    return (uint16_t)((counter->getPresetValue() >> 16) & 0xFFFF);
}

static uint16_t cb_SetCh1TargetLo(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(1);
    if (counter) {
        int32_t current = counter->getPresetValue();
        int32_t updated = (current & 0xFFFF0000) | val;
        counter->setPresetValue(updated);
    }
    reg->value = val;
    return Modbus::EX_SUCCESS;
}

static uint16_t cb_SetCh1TargetHi(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(1);
    if (counter) {
        int32_t current = counter->getPresetValue();
        int32_t updated = (current & 0x0000FFFF) | ((int32_t)val << 16);
        counter->setPresetValue(updated);
    }
    reg->value = val;
    return Modbus::EX_SUCCESS;
}

// -----------------------------------------------------------------------------
// CHANNEL 2 CALLBACKS
// -----------------------------------------------------------------------------
static uint16_t cb_GetCh2CountLo(TRegister* reg, uint16_t val) {
    int32_t count = pcnt_ch2_get_count();
    return (uint16_t)(count & 0xFFFF);
}

static uint16_t cb_GetCh2CountHi(TRegister* reg, uint16_t val) {
    int32_t count = pcnt_ch2_get_count();
    return (uint16_t)((count >> 16) & 0xFFFF);
}

static uint16_t cb_GetCh2Frequency(TRegister* reg, uint16_t val) {
    return (uint16_t)s_ch2_frequency_hz;
}

static uint16_t cb_SetCh2Reset(TRegister* reg, uint16_t val) {
    if (val == 1) {
        pcnt_ch2_reset();
        reg->value = 0; // Auto-reset register back to 0
    }
    return Modbus::EX_SUCCESS;
}

static uint16_t cb_GetCh2Mode(TRegister* reg, uint16_t val) {
    return (uint16_t)input_config_get(1).input_mode;
}

static uint16_t cb_SetCh2Mode(TRegister* reg, uint16_t val) {
    if (val > 3) return Modbus::EX_ILLEGAL_VALUE;
    change_input_mode(1, val);
    reg->value = val;
    return Modbus::EX_SUCCESS;
}

static uint16_t cb_GetCh2TargetLo(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(2);
    if (!counter) return 0;
    return (uint16_t)(counter->getPresetValue() & 0xFFFF);
}

static uint16_t cb_GetCh2TargetHi(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(2);
    if (!counter) return 0;
    return (uint16_t)((counter->getPresetValue() >> 16) & 0xFFFF);
}

static uint16_t cb_SetCh2TargetLo(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(2);
    if (counter) {
        int32_t current = counter->getPresetValue();
        int32_t updated = (current & 0xFFFF0000) | val;
        counter->setPresetValue(updated);
    }
    reg->value = val;
    return Modbus::EX_SUCCESS;
}

static uint16_t cb_SetCh2TargetHi(TRegister* reg, uint16_t val) {
    CT_counter* counter = getCounterInstance(2);
    if (counter) {
        int32_t current = counter->getPresetValue();
        int32_t updated = (current & 0x0000FFFF) | ((int32_t)val << 16);
        counter->setPresetValue(updated);
    }
    reg->value = val;
    return Modbus::EX_SUCCESS;
}

// -----------------------------------------------------------------------------
// TIMER & SYSTEM CALLBACKS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// TIMER & SYSTEM CALLBACKS
// -----------------------------------------------------------------------------


static uint16_t cb_GetOutputStatus(TRegister* reg, uint16_t val) {
    // Read the digital logic level of the output pins to report status
    uint16_t status = 0;
    if (digitalRead(OUTPUT_CH1_PIN)) status |= 0x01; // Bit 0
    if (digitalRead(OUTPUT_CH2_PIN)) status |= 0x02; // Bit 1
    return status;
}

static uint16_t cb_SetSystemReboot(TRegister* reg, uint16_t val) {
    if (val == 67) {
        delay(500); 
        ESP.restart();
    }
    return Modbus::EX_ILLEGAL_VALUE;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void modbus_init() {
    Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
    Serial.println("Modbus RTU Slave initializing...");
    
    mb.begin(&Serial2);
    mb.slave(MODBUS_SLAVE_ID);
    
    // Add all holding registers
    mb.addHreg(MB_REG_CH1_COUNT_LO, 0, MB_REG_COUNT);
    
    // --- Channel 1 Config ---
    mb.onGet(HREG(MB_REG_CH1_COUNT_LO), cb_GetCh1CountLo);
    mb.onSet(HREG(MB_REG_CH1_COUNT_LO), cb_ReadOnly);
    
    mb.onGet(HREG(MB_REG_CH1_COUNT_HI), cb_GetCh1CountHi);
    mb.onSet(HREG(MB_REG_CH1_COUNT_HI), cb_ReadOnly);
    
    mb.onGet(HREG(MB_REG_CH1_FREQUENCY), cb_GetCh1Frequency);
    mb.onSet(HREG(MB_REG_CH1_FREQUENCY), cb_ReadOnly);
    
    mb.onGet(HREG(MB_REG_CH1_RESET), cb_ReturnZero);
    mb.onSet(HREG(MB_REG_CH1_RESET), cb_SetCh1Reset);

    mb.onGet(HREG(MB_REG_CH1_MODE), cb_GetCh1Mode);
    mb.onSet(HREG(MB_REG_CH1_MODE), cb_SetCh1Mode);
    
    // Target 1
    mb.onGet(HREG(MB_REG_CH1_TARGET_LO), cb_GetCh1TargetLo);
    mb.onSet(HREG(MB_REG_CH1_TARGET_LO), cb_SetCh1TargetLo);
    mb.onGet(HREG(MB_REG_CH1_TARGET_HI), cb_GetCh1TargetHi);
    mb.onSet(HREG(MB_REG_CH1_TARGET_HI), cb_SetCh1TargetHi);
    
    // --- Channel 2 Config ---
    mb.onGet(HREG(MB_REG_CH2_COUNT_LO), cb_GetCh2CountLo);
    mb.onSet(HREG(MB_REG_CH2_COUNT_LO), cb_ReadOnly);
    
    mb.onGet(HREG(MB_REG_CH2_COUNT_HI), cb_GetCh2CountHi);
    mb.onSet(HREG(MB_REG_CH2_COUNT_HI), cb_ReadOnly);
    
    mb.onGet(HREG(MB_REG_CH2_FREQUENCY), cb_GetCh2Frequency);
    mb.onSet(HREG(MB_REG_CH2_FREQUENCY), cb_ReadOnly);
    
    mb.onGet(HREG(MB_REG_CH2_RESET), cb_ReturnZero);
    mb.onSet(HREG(MB_REG_CH2_RESET), cb_SetCh2Reset);

    mb.onGet(HREG(MB_REG_CH2_MODE), cb_GetCh2Mode);
    mb.onSet(HREG(MB_REG_CH2_MODE), cb_SetCh2Mode);

    // Target 2
    mb.onGet(HREG(MB_REG_CH2_TARGET_LO), cb_GetCh2TargetLo);
    mb.onSet(HREG(MB_REG_CH2_TARGET_LO), cb_SetCh2TargetLo);
    mb.onGet(HREG(MB_REG_CH2_TARGET_HI), cb_GetCh2TargetHi);
    mb.onSet(HREG(MB_REG_CH2_TARGET_HI), cb_SetCh2TargetHi);

    // --- System Config ---
    mb.onGet(HREG(MB_REG_OUTPUT_STATUS), cb_GetOutputStatus);
    mb.onSet(HREG(MB_REG_OUTPUT_STATUS), cb_ReadOnly);

    mb.onGet(HREG(MB_REG_OUTPUT_STATUS), cb_GetOutputStatus);
    mb.onSet(HREG(MB_REG_OUTPUT_STATUS), cb_ReadOnly);

    mb.onGet(HREG(MB_REG_SYSTEM_REBOOT), cb_ReturnZero);
    mb.onSet(HREG(MB_REG_SYSTEM_REBOOT), cb_SetSystemReboot);

    Serial.printf("Modbus RTU Slave started: ID=%d, Baud=%d\n", 
                  MODBUS_SLAVE_ID, MODBUS_BAUD_RATE);
}
