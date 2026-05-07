#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

/**
 * @file modbus_rtu.h
 * @brief Modbus RTU slave communication declarations
 * 
 * Provides RS485 Modbus RTU interface for:
 * - Reading counter values
 * - Reading frequency measurements
 * - Reading/controlling timer
 */

#include <Arduino.h>
#include <ModbusRTU.h>

// External reference to Modbus object (for mb.task() in main loop)
extern ModbusRTU mb;

/**
 * @brief Initialize Modbus RTU slave
 * Configures Serial2 and sets up register callbacks
 */
void modbus_init();

#endif // MODBUS_RTU_H
