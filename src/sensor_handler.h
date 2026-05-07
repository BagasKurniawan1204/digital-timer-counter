#ifndef SENSOR_HANDLER_H
#define SENSOR_HANDLER_H

/**
 * @file sensor_handler.h
 * @brief External sensor input handling declarations
 * 
 * Handles external trigger input for stopwatch start/stop control
 * with debounce logic.
 */

#include <Arduino.h>

/**
 * @brief Initialize sensor input pin
 */
void sensor_init();

/**
 * @brief Process sensor input (call in main loop)
 * Handles debouncing and timer start/stop logic
 */
void sensor_loop();

#endif // SENSOR_HANDLER_H
