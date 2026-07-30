#ifndef TIMER_OPERATION_H
#define TIMER_OPERATION_H

/**
 * @file timer_operation.h
 * @brief Timer and stopwatch functionality declarations
 * 
 * Provides:
 * - Frequency measurement timer (100ms interval)
 * - Stopwatch timer for elapsed time measurement
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

/**
 * @brief Initialize the stopwatch timer
 * Uses TIMER_GROUP_1, TIMER_0
 */
void counter_timer_init();

/**
 * @brief Start the counter timer
 */
void counter_timer_start();

/**
 * @brief Stop and reset the counter timer
 */
void counter_timer_stop();

/**
 * @brief Pause the counter timer (keeps current value)
 */
void counter_timer_pause();

/**
 * @brief Resume a paused counter timer
 */
void counter_timer_resume();

/**
 * @brief Reset counter timer to zero
 */
void counter_timer_reset();

/**
 * @brief Get elapsed time in microseconds
 * @param elapsed_us Pointer to store elapsed time
 */
void counter_timer_get_elapsed(uint64_t *elapsed_us);

#endif // TIMER_OPERATION_H
