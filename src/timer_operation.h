#ifndef TIMER_OPERATION_H
#define TIMER_OPERATION_H

/**
 * @file timer_operation.h
 * @brief Timer functionality declarations
 * 
 * Provides:
 * - Frequency measurement timer (100ms interval)
 */

#include <Arduino.h>
#include "driver/timer.h"

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief Timer ISR handler for frequency measurement
 * @param arg User argument (unused)
 * @return false to not yield to higher priority task
 */
bool IRAM_ATTR timer_isr_freq_handler(void *arg);

/**
 * @brief Initialize the frequency measurement timer
 * Uses TIMER_GROUP_0, TIMER_0 with 100ms interval
 */
void freq_timer_init();

#endif // TIMER_OPERATION_H
