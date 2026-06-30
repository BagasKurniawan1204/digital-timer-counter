#ifndef GLOBALS_H
#define GLOBALS_H

/**
 * @file globals.h
 * @brief Global variable declarations (extern)
 * 
 * All global variables are declared here as extern.
 * Actual definitions are in globals.cpp
 */

#include <Arduino.h>
#include "driver/pcnt.h"

// =============================================================================
// HARDWARE LOCKS (Thread safety for ISR)
// =============================================================================
extern portMUX_TYPE pcnt_spinlock;

// =============================================================================
// SYSTEM STATE FLAGS
// =============================================================================
extern bool counter_enabled;        // Master counter enable
extern bool timer_enabled;          // Master timer enable
extern bool continuous_mode;        // Continuous counting mode
#include "config.h"
extern ChannelFunction ch1_function;
extern ChannelFunction ch2_function;

// =============================================================================
// COUNTER CHANNEL 1 VARIABLES
// =============================================================================
extern volatile int32_t s_ch1_total_count;      // Accumulated count (overflow handling)
extern volatile int32_t s_ch1_current_count;    // Current count snapshot
extern volatile int32_t s_ch1_last_count;       // Last count for frequency calc
extern volatile int32_t s_ch1_frequency_hz;     // Calculated frequency
extern volatile uint32_t s_ch1_error_count;     // Error counter

// =============================================================================
// COUNTER CHANNEL 2 VARIABLES
// =============================================================================
extern volatile int32_t s_ch2_total_count;      // Accumulated count (overflow handling)
extern volatile int32_t s_ch2_current_count;    // Current count snapshot
extern volatile int32_t s_ch2_last_count;       // Last count for frequency calc
extern volatile int32_t s_ch2_frequency_hz;     // Calculated frequency
extern volatile uint32_t s_ch2_error_count;     // Error counter

// =============================================================================
// TIMER VARIABLES
// =============================================================================
// Time-related state can be added here if needed

#endif // GLOBALS_H