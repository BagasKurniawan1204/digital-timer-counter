/**
 * @file globals.cpp
 * @brief Global variable definitions
 * 
 * This file contains the actual definitions for all global variables
 * declared in globals.h
 */

#include "globals.h"

// =============================================================================
// HARDWARE LOCKS
// =============================================================================
portMUX_TYPE pcnt_spinlock = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// SYSTEM STATE FLAGS
// =============================================================================
bool counter_enabled = true;
bool timer_enabled = true;
bool continuous_mode = false;
bool stopwatch_running = false;

// =============================================================================
// COUNTER CHANNEL 1 VARIABLES
// =============================================================================
volatile int32_t s_ch1_total_count = 0;
volatile int32_t s_ch1_current_count = 0;
volatile int32_t s_ch1_last_count = 0;
volatile int32_t s_ch1_frequency_hz = 0;
volatile uint32_t s_ch1_error_count = 0;
volatile bool s_ch1_new_freq_available = false;

// =============================================================================
// COUNTER CHANNEL 2 VARIABLES
// =============================================================================
volatile int32_t s_ch2_total_count = 0;
volatile int32_t s_ch2_current_count = 0;
volatile int32_t s_ch2_last_count = 0;
volatile int32_t s_ch2_frequency_hz = 0;
volatile uint32_t s_ch2_error_count = 0;
volatile bool s_ch2_new_freq_available = false;

// =============================================================================
// TIMER VARIABLES
// =============================================================================
volatile uint32_t elapsed_ms = 0;
