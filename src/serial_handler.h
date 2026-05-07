#ifndef SERIAL_HANDLER_H
#define SERIAL_HANDLER_H

/**
 * @file serial_handler.h
 * @brief Serial command interface declarations
 * 
 * Provides debug/control interface via Serial monitor:
 * - RESET: Reset counters
 * - START: Start continuous mode
 * - STOP: Stop continuous mode
 * - PAUSE: Pause counters and timer
 * - RESUME: Resume counters and timer
 * - STATUS: Print current status
 */

#include <Arduino.h>

/**
 * @brief Process serial commands (call in main loop)
 */
void serial_handler_loop();

#endif // SERIAL_HANDLER_H
